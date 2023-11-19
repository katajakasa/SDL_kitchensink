#include <SDL_timer.h>
#include <assert.h>

#include "kitchensink/internal/kitdemuxerthread.h"
#include "kitchensink/kiterror.h"

static int Kit_DemuxMain(void *ptr) {
    Kit_DemuxerThread *thread = ptr;
    while(SDL_AtomicGet(&thread->run)) {
        if(SDL_AtomicGet(&thread->seek)) {
            Kit_DemuxerSeek(thread->demuxer, thread->seek_target);
            SDL_AtomicSet(&thread->seek, 0);
        }
        if(!Kit_RunDemuxer(thread->demuxer))
            break;
    }
    SDL_AtomicSet(&thread->run, 0);
    return 0;
}

Kit_DemuxerThread *Kit_CreateDemuxerThread(Kit_Demuxer *demuxer) {
    Kit_DemuxerThread *demuxer_thread = NULL;

    if((demuxer_thread = calloc(1, sizeof(Kit_DemuxerThread))) == NULL) {
        Kit_SetError("Unable to allocate demuxer thread");
        goto exit_0;
    }

    demuxer_thread->thread = NULL;
    demuxer_thread->demuxer = demuxer;
    demuxer_thread->seek_target = 0;
    SDL_AtomicSet(&demuxer_thread->run, 0);
    SDL_AtomicSet(&demuxer_thread->seek, 0);
    return demuxer_thread;

exit_0:
    return false;
}

void Kit_SeekDemuxerThread(Kit_DemuxerThread *demuxer_thread, int64_t seek_target) {
    if(SDL_AtomicGet(&demuxer_thread->seek))
        return;
    demuxer_thread->seek_target = seek_target;
    SDL_AtomicSet(&demuxer_thread->seek, 1);
}

void Kit_StartDemuxerThread(Kit_DemuxerThread *demuxer_thread) {
    if(!demuxer_thread || demuxer_thread->thread)
        return;
    SDL_AtomicSet(&demuxer_thread->run, 1);
    demuxer_thread->thread = SDL_CreateThread(Kit_DemuxMain, "SDL_Kitchensink demuxer thread", demuxer_thread);
}

void Kit_StopDemuxerThread(Kit_DemuxerThread *demuxer_thread) {
    if(!demuxer_thread || !demuxer_thread->thread)
        return;
    SDL_AtomicSet(&demuxer_thread->run, 0);
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
    return SDL_AtomicGet(&demuxer_thread->run);
}

void Kit_CloseDemuxerThread(Kit_DemuxerThread **ref) {
    if(!ref || !*ref)
        return;

    Kit_DemuxerThread *demuxer_thread = *ref;
    Kit_StopDemuxerThread(demuxer_thread);
    Kit_WaitDemuxerThread(demuxer_thread);
    free(demuxer_thread);
    *ref = NULL;
}
