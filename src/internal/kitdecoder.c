#include <stdlib.h>
#include <assert.h>

#include <libavformat/avformat.h>

#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/kiterror.h"

#define BUFFER_IN_SIZE 256

static void free_in_video_packet_cb(void *packet) {
    av_packet_free((AVPacket**)&packet);
}

Kit_Decoder* Kit_CreateDecoder(const Kit_Source *src, int stream_index, 
                               int out_b_size, dec_free_packet_cb free_out_cb,
                               int thread_count) {
    assert(src != NULL);
    assert(out_b_size > 0);
    assert(thread_count > 0);

    AVCodecContext *codec_ctx = NULL;
    AVCodec *codec = NULL;
    AVFormatContext *format_ctx = src->format_ctx;
    int bsizes[2] = {BUFFER_IN_SIZE, out_b_size};
    dec_free_packet_cb free_hooks[2] = {free_in_video_packet_cb, free_out_cb};

    // Make sure index seems correct
    if(stream_index >= (int)format_ctx->nb_streams || stream_index < 0) {
        Kit_SetError("Invalid stream %d", stream_index);
        goto exit_0;
    }
    
    // Allocate decoder and make sure allocation was a success
    Kit_Decoder *dec = calloc(1, sizeof(Kit_Decoder));
    if(dec == NULL) {
        Kit_SetError("Unable to allocate kit decoder for stream %d", stream_index);
        goto exit_0;
    }

    // Find audio decoder
    codec = avcodec_find_decoder(format_ctx->streams[stream_index]->codec->codec_id);
    if(!codec) {
        Kit_SetError("No suitable decoder found for stream %d", stream_index);
        goto exit_1;
    }

    // Allocate a context for the codec
    codec_ctx = avcodec_alloc_context3(codec);
    if(avcodec_copy_context(codec_ctx, format_ctx->streams[stream_index]->codec) != 0) {
        Kit_SetError("Unable to copy audio codec context for stream %d", stream_index);
        goto exit_1;
    }

    // Set thread count
    codec_ctx->thread_count = thread_count;
    codec_ctx->thread_type = FF_THREAD_SLICE;

    // Open the stream
    if(avcodec_open2(codec_ctx, codec, NULL) < 0) {
        Kit_SetError("Unable to open codec for stream %d", stream_index);
        goto exit_2;
    }

    // Set index and codec
    dec->stream_index = stream_index;
    dec->codec_ctx = codec_ctx;
    dec->format_ctx = format_ctx;

    // Allocate input/output ringbuffers and locks
    for(int i = 0; i < 2; i++) {
        dec->buffer[i] = Kit_CreateBuffer(bsizes[i], free_hooks[i]);
        if(dec->buffer[i] == NULL) {
            Kit_SetError("Unable to allocate buffer for stream %d: %s", stream_index, SDL_GetError());
            goto exit_3;
        }
    }

    // Create a lock for output buffer synchronization
    dec->output_lock = SDL_CreateMutex();
    if(dec->output_lock == NULL) {
        Kit_SetError("Unable to allocate mutex for stream %d: %s", stream_index, SDL_GetError());
        goto exit_4;
    }

    // That's that
    return dec;

exit_4:
    SDL_DestroyMutex(dec->output_lock);
exit_3:
    for(int i = 0; i < KIT_DEC_BUF_COUNT; i++) {
        Kit_DestroyBuffer(dec->buffer[i]);
    }
    avcodec_close(codec_ctx);
exit_2:
    avcodec_free_context(&codec_ctx);
exit_1:
    free(dec);
exit_0:
    return NULL;
}

void Kit_CloseDecoder(Kit_Decoder *dec) {
    if(dec == NULL) return;
    if(dec->dec_close) {
        dec->dec_close(dec);
    }
    for(int i = 0; i < KIT_DEC_BUF_COUNT; i++) {
        Kit_DestroyBuffer(dec->buffer[i]);
    }
    SDL_DestroyMutex(dec->output_lock);
    avcodec_close(dec->codec_ctx);
    avcodec_free_context(&dec->codec_ctx);
    free(dec);
}

int Kit_RunDecoder(Kit_Decoder *dec) {
    if(dec == NULL) return 0;

    AVPacket *in_packet;
    int is_output_full = 1;

    // First, check if there is room in output buffer
    if(SDL_LockMutex(dec->output_lock) == 0) {
        is_output_full = Kit_IsBufferFull(dec->buffer[KIT_DEC_BUF_OUT]);
        SDL_UnlockMutex(dec->output_lock);
    }
    if(is_output_full) {
        return 0;
    }

    // Then, see if we have incoming data
    in_packet = Kit_ReadDecoderInput(dec);
    if(in_packet == NULL) {
        return 0;
    }

    // Run decoder with incoming packet
    dec->dec_decode(dec, in_packet);

    // Free raw packet before returning
    av_packet_free(&in_packet);
    return 1;
}

// ---- Information API ----

int Kit_GetDecoderCodecInfo(const Kit_Decoder *dec, Kit_Codec *codec) {
    if(dec == NULL) {
        memset(codec, 0, sizeof(Kit_Codec));
        return 1;
    }
    codec->threads = dec->codec_ctx->thread_count;
    snprintf(codec->name, KIT_CODEC_NAME_MAX, "%s", dec->codec_ctx->codec->name);
    snprintf(codec->description, KIT_CODEC_DESC_MAX, "%s", dec->codec_ctx->codec->long_name);
    return 0;
}

int Kit_GetDecoderOutputFormat(const Kit_Decoder *dec, Kit_OutputFormat *output) {
    if(dec == NULL) {
        memset(output, 0, sizeof(Kit_OutputFormat));
        return 1;
    }
    memcpy(output, &dec->output, sizeof(Kit_OutputFormat));
    return 0;
}

int Kit_GetDecoderStreamIndex(const Kit_Decoder *dec) {
    if(dec == NULL)
        return -1;
    return dec->stream_index;
}

// ---- Clock handling ----

void Kit_SetDecoderClockSync(Kit_Decoder *dec, double sync) {
    if(dec == NULL)
        return;
    dec->clock_sync = sync;
}

void Kit_ChangeDecoderClockSync(Kit_Decoder *dec, double sync) {
    if(dec == NULL)
        return;
    dec->clock_sync += sync;
}

// ---- Input buffer handling ----

int Kit_WriteDecoderInput(Kit_Decoder *dec, AVPacket *packet) {
    assert(dec != NULL);
    return Kit_WriteBuffer(dec->buffer[KIT_DEC_BUF_IN], packet);
}

bool Kit_CanWriteDecoderInput(Kit_Decoder *dec) {
    assert(dec != NULL);
    return !Kit_IsBufferFull(dec->buffer[KIT_DEC_BUF_IN]);
}

AVPacket* Kit_ReadDecoderInput(Kit_Decoder *dec) {
    assert(dec != NULL);
    return Kit_ReadBuffer(dec->buffer[KIT_DEC_BUF_IN]);
}

void Kit_ClearDecoderInput(Kit_Decoder *dec) {
    Kit_ClearBuffer(dec->buffer[KIT_DEC_BUF_IN]);
}

// ---- Output buffer handling ----

int Kit_WriteDecoderOutput(Kit_Decoder *dec, void *packet) {
    assert(dec != NULL);
    int ret = 1;
    if(SDL_LockMutex(dec->output_lock) == 0) {
        ret = Kit_WriteBuffer(dec->buffer[KIT_DEC_BUF_OUT], packet);
        SDL_UnlockMutex(dec->output_lock);
    }
    return ret;
}

void Kit_ClearDecoderOutput(Kit_Decoder *dec) {
    if(SDL_LockMutex(dec->output_lock) == 0) {
        Kit_ClearBuffer(dec->buffer[KIT_DEC_BUF_OUT]);
        SDL_UnlockMutex(dec->output_lock);
    }
}

void* Kit_PeekDecoderOutput(Kit_Decoder *dec) {
    assert(dec != NULL);
    void *ret = NULL;
    if(SDL_LockMutex(dec->output_lock) == 0) {
        ret = Kit_PeekBuffer(dec->buffer[KIT_DEC_BUF_OUT]);
        SDL_UnlockMutex(dec->output_lock);
    }
    return ret;
}

void* Kit_ReadDecoderOutput(Kit_Decoder *dec) {
    assert(dec != NULL);
    void *ret = NULL;
    if(SDL_LockMutex(dec->output_lock) == 0) {
        ret = Kit_ReadBuffer(dec->buffer[KIT_DEC_BUF_OUT]);
        SDL_UnlockMutex(dec->output_lock);
    }
    return ret;
}

void Kit_ForEachDecoderOutput(Kit_Decoder *dec, Kit_ForEachItemCallback cb, void *userdata) {
    assert(dec != NULL);
    if(SDL_LockMutex(dec->output_lock) == 0) {
        Kit_ForEachItemInBuffer(dec->buffer[KIT_DEC_BUF_OUT], cb, userdata);
        SDL_UnlockMutex(dec->output_lock);
    }
}

void Kit_AdvanceDecoderOutput(Kit_Decoder *dec) {
    assert(dec != NULL);
    if(SDL_LockMutex(dec->output_lock) == 0) {
        Kit_AdvanceBuffer(dec->buffer[KIT_DEC_BUF_OUT]);
        SDL_UnlockMutex(dec->output_lock);
    }
}

void Kit_ClearDecoderBuffers(Kit_Decoder *dec) {
    if(dec == NULL) return;
    Kit_ClearDecoderInput(dec);
    Kit_ClearDecoderOutput(dec);
    avcodec_flush_buffers(dec->codec_ctx);
}

int Kit_LockDecoderOutput(Kit_Decoder *dec) {
    return SDL_LockMutex(dec->output_lock);
}

void Kit_UnlockDecoderOutput(Kit_Decoder *dec) {
    SDL_UnlockMutex(dec->output_lock);
}
