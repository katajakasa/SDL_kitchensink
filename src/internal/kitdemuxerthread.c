#include <SDL_timer.h>
#include <assert.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitdemuxerthread.h"
#include "kitchensink/internal/utils/kitlog.h"


static int Kit_DemuxMain(void *ptr) {
    Kit_DemuxerThread *thread = ptr;
    while (SDL_AtomicGet(&thread->run) && Kit_RunDemuxer(thread->demuxer)) {};
    LOG("DEMUXER THREAD CLOSED\n");
    return 0;
}

Kit_DemuxerThread* Kit_CreateDemuxerThread(
    const Kit_Source *src,
    int video_index,
    int audio_index,
    int subtitle_index
) {
    Kit_DemuxerThread *demuxer_thread = NULL;
    Kit_Demuxer *demuxer = NULL;

    if ((demuxer_thread = calloc(1, sizeof(Kit_DemuxerThread))) == NULL) {
        Kit_SetError("Unable to allocate demuxer thread");
        goto error_0;
    }
    if ((demuxer = Kit_CreateDemuxer(src, video_index, audio_index, subtitle_index)) == NULL) {
        goto error_1;
    }

    demuxer_thread->thread = NULL;
    demuxer_thread->demuxer = demuxer;
    SDL_AtomicSet(&demuxer_thread->run, 0);
    return demuxer_thread;

error_1:
    free(demuxer_thread);
error_0:
    return NULL;
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
    SDL_WaitThread(demuxer_thread->thread, NULL);
    demuxer_thread->thread = NULL;
}

Kit_PacketBuffer* Kit_GetDemuxerThreadPacketBuffer(const Kit_DemuxerThread *demuxer_thread, KitBufferIndex buffer_index) {
    assert(demuxer_thread);
    return Kit_GetDemuxerPacketBuffer(demuxer_thread->demuxer, buffer_index);
}

void Kit_CloseDemuxerThread(Kit_DemuxerThread **ref) {
    if (!ref || !*ref)
        return;

    Kit_DemuxerThread *demuxer_thread = *ref;
    Kit_StopDemuxerThread(demuxer_thread);
    Kit_CloseDemuxer(&demuxer_thread->demuxer);
    free(demuxer_thread);
    *ref = NULL;
}
