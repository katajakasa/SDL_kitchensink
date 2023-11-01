#include <stdlib.h>
#include <assert.h>

#include <libavformat/avformat.h>

#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/kiterror.h"


Kit_Decoder* Kit_CreateDecoder(
    AVStream *stream,
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
        codec_ctx->thread_count = 1;  // Disable threading
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
    decoder->clock_sync = -1.0;
    decoder->clock_pos = 0;
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
    if (!ref || !*ref)
        return;
    Kit_Decoder *decoder = *ref;
    if(decoder->dec_close)
        decoder->dec_close(decoder);
    avcodec_close(decoder->codec_ctx);
    avcodec_free_context(&decoder->codec_ctx);
    free(decoder);
    *ref = NULL;
}

bool Kit_RunDecoder(const Kit_Decoder *decoder) {
    assert(decoder);
    return decoder->dec_decode(decoder);
}

bool Kit_AddDecoderPacket(const Kit_Decoder *decoder, const AVPacket *packet) {
    assert(decoder);
    assert(packet);
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

void Kit_ChangeDecoderClockSync(Kit_Decoder *decoder, double sync) {
    if(!decoder)
        return;
    decoder->clock_sync += sync;
}

int Kit_GetDecoderStreamIndex(const Kit_Decoder *decoder) {
    if(!decoder)
        return -1;
    return decoder->stream->index;
}

double Kit_GetDecoderPTS(const Kit_Decoder *decoder) {
    if(!decoder)
        return -1.0;
    return decoder->clock_pos;
}
