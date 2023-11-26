#include <assert.h>
#include <stdlib.h>

#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>

#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/utils/kitlog.h"
#include "kitchensink/kiterror.h"

void Kit_PrintAVMethod(int methods) {
    LOG(" * Methods:\n");
    if(methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
        LOG("   * AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX\n");
    }
    if(methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX) {
        LOG("   * AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX\n");
    }
    if(methods & AV_CODEC_HW_CONFIG_METHOD_INTERNAL) {
        LOG("   * AV_CODEC_HW_CONFIG_METHOD_INTERNAL\n");
    }
    if(methods & AV_CODEC_HW_CONFIG_METHOD_AD_HOC) {
        LOG("   * AV_CODEC_HW_CONFIG_METHOD_AD_HOC\n");
    }
}

void Kit_PrintPixelFormatType(enum AVPixelFormat pix_fmt) {
    LOG(" * Pixel format: %s\n", av_get_pix_fmt_name(pix_fmt));
}

void Kit_PrintAVType(enum AVHWDeviceType type) {
    LOG(" * Type: ");
    switch(type) {
        case AV_HWDEVICE_TYPE_NONE:
            LOG("AV_HWDEVICE_TYPE_NONE\n");
            break;
        case AV_HWDEVICE_TYPE_VDPAU:
            LOG("AV_HWDEVICE_TYPE_VDPAU\n");
            break;
        case AV_HWDEVICE_TYPE_CUDA:
            LOG("AV_HWDEVICE_TYPE_CUDA\n");
            break;
        case AV_HWDEVICE_TYPE_VAAPI:
            LOG("AV_HWDEVICE_TYPE_VAAPI\n");
            break;
        case AV_HWDEVICE_TYPE_DXVA2:
            LOG("AV_HWDEVICE_TYPE_DXVA2\n");
            break;
        case AV_HWDEVICE_TYPE_QSV:
            LOG("AV_HWDEVICE_TYPE_QSV\n");
            break;
        case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
            LOG("AV_HWDEVICE_TYPE_VIDEOTOOLBOX\n");
            break;
        case AV_HWDEVICE_TYPE_D3D11VA:
            LOG("AV_HWDEVICE_TYPE_D3D11VA\n");
            break;
        case AV_HWDEVICE_TYPE_DRM:
            LOG("AV_HWDEVICE_TYPE_DRM\n");
            break;
        case AV_HWDEVICE_TYPE_OPENCL:
            LOG("AV_HWDEVICE_TYPE_OPENCL\n");
            break;
        case AV_HWDEVICE_TYPE_MEDIACODEC:
            LOG("AV_HWDEVICE_TYPE_MEDIACODEC\n");
            break;
        case AV_HWDEVICE_TYPE_VULKAN:
            LOG("AV_HWDEVICE_TYPE_VULKAN\n");
            break;
    }
}

static void Kit_PrintHardwareDecoders(const AVCodec *codec) {
    const AVCodecHWConfig *config;
    int index = 0;
    while((config = avcodec_get_hw_config(codec, index)) != NULL) {
        LOG("Device %d\n", index);
        Kit_PrintAVMethod(config->methods);
        Kit_PrintAVType(config->device_type);
        Kit_PrintPixelFormatType(config->pix_fmt);
        index++;
    }
}

static bool Kit_FindHardwareDecoder(const AVCodec *codec, enum AVHWDeviceType *type) {
    const AVCodecHWConfig *config;
    int index = 0;
    while((config = avcodec_get_hw_config(codec, index++)) != NULL) {
        if(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
           config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
            *type = config->device_type;
            return true;
        }
    }
    return false;
}

static enum AVPixelFormat Kit_GetHardwarePixelFormat(AVCodecContext *ctx, const enum AVPixelFormat *formats) {
    while(*formats != AV_PIX_FMT_NONE) {
        LOG("FMT %s\n", av_get_pix_fmt_name(*formats));
        switch(*formats) {
            case AV_PIX_FMT_YUV420P10:
                return AV_PIX_FMT_YUV420P10;
            case AV_PIX_FMT_D3D11VA_VLD:
                return AV_PIX_FMT_D3D11;
            case AV_PIX_FMT_YUV420P:
            case AV_PIX_FMT_YUYV422:
            case AV_PIX_FMT_UYVY422:
            case AV_PIX_FMT_NV12:
            case AV_PIX_FMT_NV21:
            case AV_PIX_FMT_CUDA:
            case AV_PIX_FMT_QSV:
            case AV_PIX_FMT_DXVA2_VLD:
            case AV_PIX_FMT_D3D11:
            case AV_PIX_FMT_VAAPI:
            case AV_PIX_FMT_VDPAU:
                return *formats;
            default:;
        }
        formats++;
    }
    return AV_PIX_FMT_NONE;
}

static bool Kit_InitHardwareDecoder(AVCodecContext *ctx, const enum AVHWDeviceType type) {
    AVBufferRef *hw_device_ctx = NULL;
    int err = av_hwdevice_ctx_create(&hw_device_ctx, type, NULL, NULL, 0);
    if(err < 0) {
        Kit_SetError("Unable to create hardware device -- %s", av_err2str(err));
        return false;
    }
    ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    return true;
}

Kit_Decoder *Kit_CreateDecoder(
    AVStream *stream,
    Kit_Timer *sync_timer,
    int thread_count,
    dec_input_cb dec_input,
    dec_decode_cb dec_decode,
    dec_flush_cb dec_flush,
    dec_signal_cb dec_signal,
    dec_close_cb dec_close,
    void *userdata
) {
    assert(stream != NULL);
    assert(thread_count >= 0);

    enum AVHWDeviceType hw_type;
    Kit_Decoder *decoder = NULL;
    AVCodecContext *codec_ctx = NULL;
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
    codec_ctx->thread_count = thread_count;
    if(codec->capabilities | AV_CODEC_CAP_FRAME_THREADS) {
        codec_ctx->thread_type = FF_THREAD_FRAME;
    } else if(codec->capabilities | AV_CODEC_CAP_SLICE_THREADS) {
        codec_ctx->thread_type = FF_THREAD_SLICE;
    } else {
        codec_ctx->thread_count = 1; // Disable threading
    }

    Kit_PrintHardwareDecoders(codec);
    if(Kit_FindHardwareDecoder(codec, &hw_type)) {
        codec_ctx->get_format = Kit_GetHardwarePixelFormat;
        if(!Kit_InitHardwareDecoder(codec_ctx, hw_type))
            goto exit_2;
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
    decoder->sync_timer = sync_timer;
    decoder->codec_ctx = codec_ctx;
    decoder->dec_input = dec_input;
    decoder->dec_decode = dec_decode;
    decoder->dec_flush = dec_flush;
    decoder->dec_signal = dec_signal;
    decoder->dec_close = dec_close;
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
    avcodec_close(decoder->codec_ctx);
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

int Kit_GetDecoderStreamIndex(const Kit_Decoder *decoder) {
    if(!decoder)
        return -1;
    return decoder->stream->index;
}
