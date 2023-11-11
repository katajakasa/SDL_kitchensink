#include <SDL_thread.h>
#include <SDL_timer.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitdecoderthread.h"
#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/utils/kitlog.h"


static void Kit_ProcessPacket(Kit_DecoderThread *thread, bool *pts_jumped) {
    if(!Kit_BeginPacketBufferRead(thread->input, thread->scratch_packet, 10))
        return;

    // If a valid packet was found, first check if it's a control packet. Value 1 means seek.
    // Seek packet is created in the demuxer, and is sent after avformat_seek_file() is called.
    if(thread->scratch_packet->opaque == (void*)1) {
        Kit_ClearDecoderBuffers(thread->decoder);
        *pts_jumped = true;
        goto finish;
    }

    // If valid packet was found and it is not a control packet, it must contain stream data.
    // Attempt to add it to the ffmpeg decoder internal queue. Note that the queue may be full, in which case
    // we try again later.
    if(Kit_AddDecoderPacket(thread->decoder, thread->scratch_packet)) {
        goto finish;
    } else {
        goto cancel;
    }

finish:
    Kit_FinishPacketBufferRead(thread->input);
    av_packet_unref(thread->scratch_packet);
    return;

cancel:
    Kit_CancelPacketBufferRead(thread->input);
    av_packet_unref(thread->scratch_packet);
    return;
}


static int Kit_DecodeMain(void *ptr) {
    Kit_DecoderThread *thread = ptr;
    bool pts_jumped = false;
    double pts;

    while(SDL_AtomicGet(&thread->run)) {
        Kit_ProcessPacket(thread, &pts_jumped);

        // Run the decoder. This will consume packets from the ffmpeg queue. We may need to call this multiple times,
        // since a single data packet might contain multiple frames.
        while(SDL_AtomicGet(&thread->run) && Kit_RunDecoder(thread->decoder, &pts)) {
            if(pts_jumped) {
                Kit_AdjustTimerBase(thread->decoder->sync_timer, pts);
                pts_jumped = false;
            }
        }
    }

    return 0;
}

Kit_DecoderThread* Kit_CreateDecoderThread(
    Kit_PacketBuffer *input,
    Kit_Decoder *decoder
) {
    Kit_DecoderThread *dec_thread;
    AVPacket *packet;

    if((packet = av_packet_alloc()) == NULL) {
        Kit_SetError("Unable to allocate decoder scratch packet");
        goto error_0;
    }
    if((dec_thread = calloc(1, sizeof(Kit_DecoderThread))) == NULL) {
        Kit_SetError("Unable to allocate decoder thread");
        goto error_1;
    }

    dec_thread->input = input;
    dec_thread->decoder = decoder;
    dec_thread->scratch_packet = packet;
    SDL_AtomicSet(&dec_thread->run, 0);
    return dec_thread;

error_1:
    av_packet_free(&packet);
error_0:
    return NULL;
}

void Kit_StartDecoderThread(Kit_DecoderThread *dec_thread, const char *name) {
    if(!dec_thread || dec_thread->thread)
        return;
    SDL_AtomicSet(&dec_thread->run, 1);
    dec_thread->thread = SDL_CreateThread(Kit_DecodeMain, name, dec_thread);
}

void Kit_StopDecoderThread(Kit_DecoderThread *dec_thread) {
    if(!dec_thread || !dec_thread->thread)
        return;
    SDL_AtomicSet(&dec_thread->run, 0);
}

void Kit_WaitDecoderThread(Kit_DecoderThread *dec_thread) {
    if(!dec_thread || !dec_thread->thread)
        return;
    SDL_WaitThread(dec_thread->thread, NULL);
    dec_thread->thread = NULL;
}

void Kit_CloseDecoderThread(Kit_DecoderThread **ref) {
    if (!ref || !*ref)
        return;
    Kit_DecoderThread *dec_thread = *ref;
    Kit_StopDecoderThread(dec_thread);
    Kit_WaitDecoderThread(dec_thread);
    av_packet_free(&dec_thread->scratch_packet);
    free(dec_thread);
    *ref = NULL;
}
