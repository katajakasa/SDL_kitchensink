#ifndef KITDEMUXERTHREAD_H
#define KITDEMUXERTHREAD_H

#include "kitchensink2/internal/kitbufferindex.h"
#include "kitchensink2/internal/kitdemuxer.h"
#include "kitchensink2/internal/kitpacketbuffer.h"
#include "kitchensink2/kitconfig.h"
#include "kitchensink2/kitsource.h"
#include <SDL_thread.h>
#include <stdbool.h>

typedef struct Kit_DemuxerThread {
    Kit_Demuxer *demuxer;
    SDL_Thread *thread;
    SDL_atomic_t run;
    SDL_atomic_t seek;
    int64_t seek_target;
} Kit_DemuxerThread;

KIT_LOCAL Kit_DemuxerThread *Kit_CreateDemuxerThread(Kit_Demuxer *demuxer);
KIT_LOCAL void Kit_CloseDemuxerThread(Kit_DemuxerThread **demuxer);

KIT_LOCAL void Kit_SeekDemuxerThread(Kit_DemuxerThread *demuxer_thread, int64_t seek_target);
KIT_LOCAL Kit_PacketBuffer *
Kit_GetDemuxerThreadPacketBuffer(const Kit_DemuxerThread *demuxer_thread, Kit_BufferIndex buffer_index);

KIT_LOCAL void Kit_StartDemuxerThread(Kit_DemuxerThread *demuxer_thread);
KIT_LOCAL void Kit_StopDemuxerThread(Kit_DemuxerThread *demuxer_thread);
KIT_LOCAL void Kit_WaitDemuxerThread(Kit_DemuxerThread *demuxer_thread);
KIT_LOCAL bool Kit_IsDemuxerThreadAlive(Kit_DemuxerThread *demuxer_thread);

#endif // KITDEMUXERTHREAD_H
