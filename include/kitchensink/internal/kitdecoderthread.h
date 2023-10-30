#ifndef KITDECODERTHREAD_H
#define KITDECODERTHREAD_H

#include <stdbool.h>
#include <SDL_thread.h>
#include "kitchensink/kitconfig.h"
#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/kitpacketbuffer.h"

typedef struct Kit_DecoderThread {
    Kit_PacketBuffer *input;
    Kit_Decoder *decoder;
    SDL_Thread *thread;
    AVPacket *scratch_packet;
    SDL_atomic_t run;
} Kit_DecoderThread;

KIT_LOCAL Kit_DecoderThread* Kit_CreateDecoderThread(Kit_PacketBuffer *input, Kit_Decoder *decoder);
KIT_LOCAL void Kit_StartDecoderThread(Kit_DecoderThread *dec_thread, const char *name);
KIT_LOCAL void Kit_StopDecoderThread(Kit_DecoderThread *dec_thread);
KIT_LOCAL void Kit_CloseDecoderThread(Kit_DecoderThread **ref);

#endif // KITDECODERTHREAD_H
