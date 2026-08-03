#include <SDL3/SDL_timer.h>
#include <assert.h>

#include "kitchensink3/internal/kitdemuxerthread.h"
#include "kitchensink3/internal/utils/kitalloc.h"
#include "kitchensink3/kiterror.h"

static int Kit_DemuxMain(void *ptr) {
    Kit_DemuxerThread *thread = ptr;
    bool eof = false;

    while(SDL_GetAtomicInt(&thread->run)) {
        if(thread->seek) {
            // Seeks are only requested while the demuxer thread is stopped, no locks needed.
            thread->seek = false;
            Kit_DemuxerSeek(thread->demuxer, thread->timer, thread->seek_target);
        }
        if(!Kit_RunDemuxer(thread->demuxer)) {
            eof = true;
            break;
        }
    }

    SDL_SetAtomicInt(&thread->run, 0);
    if(eof) {
        for(int i = 0; i < KIT_INDEX_COUNT; i++) {
            Kit_SendDemuxerEOFPacket(thread->demuxer, i);
        }
    }
    return 0;
}

Kit_DemuxerThread *Kit_CreateDemuxerThread(Kit_Demuxer *demuxer, const Kit_Timer *timer) {
    Kit_DemuxerThread *demuxer_thread = NULL;
    Kit_Timer *seek_timer = NULL;

    if((demuxer_thread = Kit_Calloc(1, sizeof(Kit_DemuxerThread))) == NULL) {
        Kit_SetError("Unable to allocate demuxer thread");
        goto exit_0;
    }
    if((seek_timer = Kit_CreateSecondaryTimer(timer, false)) == NULL) {
        Kit_SetError("Unable to allocate demuxer thread timer");
        goto exit_1;
    }

    demuxer_thread->thread = NULL;
    demuxer_thread->demuxer = demuxer;
    demuxer_thread->seek = false;
    demuxer_thread->seek_target = 0;
    demuxer_thread->timer = seek_timer;
    SDL_SetAtomicInt(&demuxer_thread->run, 0);
    return demuxer_thread;

exit_1:
    free(demuxer_thread);
exit_0:
    return NULL;
}

void Kit_SeekDemuxerThread(Kit_DemuxerThread *demuxer_thread, int64_t seek_target) {
    assert(demuxer_thread->thread == NULL);
    demuxer_thread->seek_target = seek_target;
    demuxer_thread->seek = true;
}

void Kit_StartDemuxerThread(Kit_DemuxerThread *demuxer_thread) {
    if(!demuxer_thread || demuxer_thread->thread)
        return;
    SDL_SetAtomicInt(&demuxer_thread->run, 1);
    demuxer_thread->thread = SDL_CreateThread(Kit_DemuxMain, "SDL_Kitchensink demuxer thread", demuxer_thread);
}

void Kit_StopDemuxerThread(Kit_DemuxerThread *demuxer_thread) {
    if(!demuxer_thread || !demuxer_thread->thread)
        return;
    SDL_SetAtomicInt(&demuxer_thread->run, 0);
}

void Kit_WaitDemuxerThread(Kit_DemuxerThread *demuxer_thread) {
    if(!demuxer_thread || !demuxer_thread->thread)
        return;
    SDL_WaitThread(demuxer_thread->thread, NULL);
    demuxer_thread->thread = NULL;
}

Kit_PacketBuffer *
Kit_GetDemuxerThreadPacketBuffer(const Kit_DemuxerThread *demuxer_thread, Kit_BufferIndex buffer_index) {
    assert(demuxer_thread);
    return Kit_GetDemuxerPacketBuffer(demuxer_thread->demuxer, buffer_index);
}

bool Kit_IsDemuxerThreadAlive(Kit_DemuxerThread *demuxer_thread) {
    return SDL_GetAtomicInt(&demuxer_thread->run);
}

void Kit_CloseDemuxerThread(Kit_DemuxerThread **ref) {
    if(!ref || !*ref)
        return;

    Kit_DemuxerThread *demuxer_thread = *ref;
    Kit_StopDemuxerThread(demuxer_thread);
    Kit_WaitDemuxerThread(demuxer_thread);
    Kit_CloseTimer(&demuxer_thread->timer);
    free(demuxer_thread);
    *ref = NULL;
}
