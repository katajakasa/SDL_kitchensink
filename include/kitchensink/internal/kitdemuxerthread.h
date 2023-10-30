#ifndef KITDEMUXERTHREAD_H
#define KITDEMUXERTHREAD_H

#include <SDL_thread.h>
#include <stdbool.h>
#include "kitchensink/kitsource.h"
#include "kitchensink/kitconfig.h"
#include "kitchensink/internal/kitpacketbuffer.h"
#include "kitchensink/internal/kitbufferindex.h"
#include "kitchensink/internal/kitdemuxer.h"


typedef struct Kit_DemuxerThread {
    Kit_Demuxer *demuxer;
    SDL_Thread *thread;
    SDL_atomic_t run;
} Kit_DemuxerThread;

KIT_LOCAL Kit_DemuxerThread* Kit_CreateDemuxerThread(
    const Kit_Source *src,
    int video_index,
    int audio_index,
    int subtitle_index
);
KIT_LOCAL void Kit_CloseDemuxerThread(Kit_DemuxerThread **demuxer);

KIT_LOCAL Kit_PacketBuffer* Kit_GetDemuxerThreadPacketBuffer(const Kit_DemuxerThread *demuxer_thread, KitBufferIndex buffer_index);

KIT_LOCAL void Kit_StartDemuxerThread(Kit_DemuxerThread *demuxer_thread);
KIT_LOCAL void Kit_StopDemuxerThread(Kit_DemuxerThread *demuxer_thread);

#endif // KITDEMUXERTHREAD_H
