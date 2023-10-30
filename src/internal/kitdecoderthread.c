#include <SDL_thread.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitdecoderthread.h"
#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/utils/kitlog.h"


static int Kit_DecodeMain(void *ptr) {
    Kit_DecoderThread *thread = ptr;

    while(SDL_AtomicGet(&thread->run)) {
        if(Kit_ReadPacketBuffer(thread->input, thread->scratch_packet, 100)) {
            if(Kit_AddDecoderPacket(thread->decoder, thread->scratch_packet)) {
                av_packet_unref(thread->scratch_packet);
            }
        }
        while(SDL_AtomicGet(&thread->run) && Kit_RunDecoder(thread->decoder));
    }

    LOG("DECODER THREAD '%s' CLOSED\n", SDL_GetThreadName(thread->thread));
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
    SDL_WaitThread(dec_thread->thread, NULL);
    dec_thread->thread = NULL;
}

void Kit_CloseDecoderThread(Kit_DecoderThread **ref) {
    if (!ref || !*ref)
        return;
    Kit_DecoderThread *dec_thread = *ref;
    Kit_StopDecoderThread(dec_thread);
    free(dec_thread);
    *ref = NULL;
}
