#include <assert.h>
#include <stdlib.h>

#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

#include "kitchensink2/internal/kitdecoder.h"
#include "kitchensink2/internal/kitlibstate.h"
#include "kitchensink2/internal/utils/kitlog.h"
#include "kitchensink2/internal/video/kitvideoutils.h"
#include "kitchensink2/kiterror.h"

/**
 * Check if hardware context supports an output format that we can feed to swscale
 */
static bool Kit_TestSWFormat(const AVHWFramesConstraints *constraints) {
    enum AVPixelFormat *test;
    for(test = constraints->valid_sw_formats; *test != AV_PIX_FMT_NONE; test++) {
        if(sws_isSupportedInput(*test)) {
            return true;
        }
    }
    return false;
}

/**
 * Make sure our hardware device seems like it can handle the video stream we want to feed it.
 */
static AVBufferRef *Kit_TestHWDevice(const AVCodecHWConfig *config, unsigned int w, unsigned int h) {
    AVBufferRef *hw_device_ctx;
    AVHWFramesConstraints *constraints;

    if(!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
        goto exit_0;
    }
    if(av_hwdevice_ctx_create(&hw_device_ctx, config->device_type, NULL, NULL, 0) != 0) {
        goto exit_0;
    }
    if((constraints = av_hwdevice_get_hwframe_constraints(hw_device_ctx, NULL)) == NULL) {
        goto exit_1;
    }
    if(constraints->max_height < h || constraints->min_height > h) {
        goto exit_2;
    }
    if(constraints->max_width < w || constraints->min_width > w) {
        goto exit_2;
    }
    if(!Kit_TestSWFormat(constraints)) {
        goto exit_2;
    }

    av_hwframe_constraints_free(&constraints);
    return hw_device_ctx;

exit_2:
    av_hwframe_constraints_free(&constraints);
exit_1:
    av_buffer_unref(&hw_device_ctx);
exit_0:
    return NULL;
}

/**
 * Find an actual hardware decoder we can use. It must be able to decode our stream, and must output something
 * we can actually deal with or convert.
 */
static AVBufferRef *Kit_FindHardwareDecoder(
    const AVCodec *codec,
    unsigned int hw_device_types,
    unsigned int w,
    unsigned int h,
    enum AVHWDeviceType *type,
    enum AVPixelFormat *hw_fmt
) {
    const AVCodecHWConfig *config;

    for(int index = 0; (config = avcodec_get_hw_config(codec, index)) != NULL; index++) {
        Kit_HardwareDeviceType kit_type = Kit_FindHWDeviceType(config->device_type);
        AVBufferRef *hw_device_ctx = Kit_TestHWDevice(config, w, h);
        if(hw_device_types & kit_type && hw_device_ctx != NULL) {
            *type = config->device_type;
            *hw_fmt = config->pix_fmt;
            return hw_device_ctx;
        }
    }
    return NULL;
}

/**
 * Attempt to negotiate a hardware pixel format. If we can't find a good one, then just fall back to the
 * software format and disregard hardware. According to ffmpeg docs, it always suggests software format last,
 * so use that as a fallback.
 */
static enum AVPixelFormat Kit_GetHardwarePixelFormat(AVCodecContext *ctx, const enum AVPixelFormat *formats) {
    Kit_Decoder *decoder = ctx->opaque;
    enum AVPixelFormat prev = AV_PIX_FMT_NONE;
    const enum AVPixelFormat *fmt;
    for(fmt = formats; *fmt != AV_PIX_FMT_NONE; fmt++) {
        if(decoder->hw_fmt == *fmt) {
            return *fmt;
        }
        prev = *fmt;
    }
    // This is the last format (software format!) or AV_PIX_FMT_NONE.
    return prev;
}

Kit_Decoder *Kit_CreateDecoder(
    AVStream *stream,
    Kit_Timer *sync_timer,
    int thread_count,
    unsigned int hw_device_types,
    dec_input_cb dec_input,
    dec_decode_cb dec_decode,
    dec_flush_cb dec_flush,
    dec_signal_cb dec_signal,
    dec_close_cb dec_close,
    dec_get_buffers_cb dec_get_buffers,
    void *userdata
) {
    assert(stream != NULL);
    assert(thread_count >= 0);

    enum AVHWDeviceType hw_type = AV_HWDEVICE_TYPE_NONE;
    enum AVPixelFormat hw_fmt = AV_PIX_FMT_NONE;
    Kit_Decoder *decoder = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVBufferRef *hw_device_ctx = NULL;
    AVDictionary *codec_opts = NULL;
    const AVCodec *codec = NULL;

    if((decoder = calloc(1, sizeof(Kit_Decoder))) == NULL) {
        Kit_SetError("Unable to allocate kit decoder for stream %d", stream->index);
        goto exit_0;
    }
    if((codec = avcodec_find_decoder(stream->codecpar->codec_id)) == NULL) {
        Kit_SetError("No suitable decoder found for stream %d", stream->index);
        goto exit_1;
    }
    if((codec_ctx = avcodec_alloc_context3(codec)) == NULL) {
        Kit_SetError("Unable to allocate codec context for stream %d", stream->index);
        goto exit_1;
    }
    if(avcodec_parameters_to_context(codec_ctx, stream->codecpar) < 0) {
        Kit_SetError("Unable to copy codec context for stream %d", stream->index);
        goto exit_2;
    }
    codec_ctx->pkt_timebase = stream->time_base;
    codec_ctx->opaque = decoder; // Used by Kit_GetHardwarePixelFormat()

    // Attempt to set up threading, if supported.
    codec_ctx->thread_count = thread_count;
    if(codec->capabilities | AV_CODEC_CAP_FRAME_THREADS) {
        codec_ctx->thread_type = FF_THREAD_FRAME;
    } else if(codec->capabilities | AV_CODEC_CAP_SLICE_THREADS) {
        codec_ctx->thread_type = FF_THREAD_SLICE;
    } else {
        codec_ctx->thread_count = 1; // Disable threading
    }

    // Try to initialize the hardware decoder for this codec.
    bool is_hw_enabled = Kit_GetLibraryState()->init_flags & KIT_INIT_HW_DECODE;
    if(is_hw_enabled) {
        hw_device_ctx =
            Kit_FindHardwareDecoder(codec, hw_device_types, codec_ctx->width, codec_ctx->height, &hw_type, &hw_fmt);
        if(hw_device_ctx != NULL) {
            codec_ctx->get_format = Kit_GetHardwarePixelFormat;
            codec_ctx->hw_device_ctx = hw_device_ctx;
        }
    }

    // Open the stream with selected options. Note that av_dict_set will allocate the dict!
    // This is required for ass_process_chunk()
    av_dict_set(&codec_opts, "sub_text_format", "ass", 0);
    if(avcodec_open2(codec_ctx, codec, &codec_opts) < 0) {
        Kit_SetError("Unable to open codec for stream %d", stream->index);
        goto exit_2;
    }
    av_dict_free(&codec_opts);

    decoder->stream = stream;
    decoder->stream = stream;
    decoder->sync_timer = sync_timer;
    decoder->codec_ctx = codec_ctx;
    decoder->hw_fmt = hw_fmt;
    decoder->hw_type = hw_type;
    decoder->dec_input = dec_input;
    decoder->dec_decode = dec_decode;
    decoder->dec_flush = dec_flush;
    decoder->dec_signal = dec_signal;
    decoder->dec_close = dec_close;
    decoder->dec_get_buffers = dec_get_buffers;
    decoder->userdata = userdata;
    return decoder;

exit_2:
    av_dict_free(&codec_opts);
    avcodec_free_context(&codec_ctx);
exit_1:
    free(decoder);
exit_0:
    return NULL;
}

void Kit_CloseDecoder(Kit_Decoder **ref) {
    if(!ref || !*ref)
        return;
    Kit_Decoder *decoder = *ref;
    if(decoder->dec_close)
        decoder->dec_close(decoder);
    avcodec_free_context(&decoder->codec_ctx);
    Kit_CloseTimer(&decoder->sync_timer);
    free(decoder);
    *ref = NULL;
}

bool Kit_RunDecoder(const Kit_Decoder *decoder, double *pts) {
    assert(decoder);
    return decoder->dec_decode(decoder, pts);
}

Kit_DecoderInputResult Kit_AddDecoderPacket(const Kit_Decoder *decoder, const AVPacket *packet) {
    assert(decoder);
    return decoder->dec_input(decoder, packet);
}

void Kit_SignalDecoder(Kit_Decoder *decoder) {
    if(decoder == NULL)
        return;
    if(decoder->dec_signal)
        decoder->dec_signal(decoder);
}

void Kit_ClearDecoderBuffers(Kit_Decoder *decoder) {
    if(decoder == NULL)
        return;
    if(decoder->dec_flush)
        decoder->dec_flush(decoder);
    avcodec_flush_buffers(decoder->codec_ctx);
}

int Kit_GetDecoderCodecInfo(const Kit_Decoder *decoder, Kit_Codec *codec) {
    if(decoder == NULL) {
        memset(codec, 0, sizeof(Kit_Codec));
        return 1;
    }
    codec->threads = decoder->codec_ctx->thread_count;
    snprintf(codec->name, KIT_CODEC_NAME_MAX, "%s", decoder->codec_ctx->codec->name);
    snprintf(codec->description, KIT_CODEC_DESC_MAX, "%s", decoder->codec_ctx->codec->long_name);
    return 0;
}

int Kit_GetDecoderBufferState(const Kit_Decoder *decoder, unsigned int *length, unsigned int *capacity, size_t *bytes) {
    if(decoder && decoder->dec_get_buffers) {
        decoder->dec_get_buffers(decoder, length, capacity, bytes);
        return 0;
    }
    return 1;
}

int Kit_GetDecoderStreamIndex(const Kit_Decoder *decoder) {
    if(!decoder)
        return -1;
    return decoder->stream->index;
}
