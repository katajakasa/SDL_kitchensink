#ifndef KITDECODERTHREAD_H
#define KITDECODERTHREAD_H

#include "kitchensink2/internal/kitdecoder.h"
#include "kitchensink2/internal/kitpacketbuffer.h"
#include "kitchensink2/kitconfig.h"
#include <SDL_thread.h>
#include <stdbool.h>

typedef struct Kit_DecoderThread {
    Kit_PacketBuffer *input;
    Kit_Decoder *decoder;
    SDL_Thread *thread;
    AVPacket *scratch_packet;
    SDL_atomic_t run;
} Kit_DecoderThread;

KIT_LOCAL Kit_DecoderThread *Kit_CreateDecoderThread(Kit_PacketBuffer *input, Kit_Decoder *decoder);
KIT_LOCAL void Kit_StartDecoderThread(Kit_DecoderThread *decoder_thread, const char *name);
KIT_LOCAL void Kit_StopDecoderThread(Kit_DecoderThread *decoder_thread);
KIT_LOCAL void Kit_WaitDecoderThread(Kit_DecoderThread *decoder_thread);
KIT_LOCAL void Kit_CloseDecoderThread(Kit_DecoderThread **ref);
KIT_LOCAL bool Kit_IsDecoderThreadAlive(Kit_DecoderThread *decoder_thread);

#endif // KITDECODERTHREAD_H
