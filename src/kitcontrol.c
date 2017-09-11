
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <ass/ass.h>
#include <SDL2/SDL.h>

#include "kitchensink/internal/kitbuffer.h"
#include "kitchensink/internal/kitlist.h"
#include "kitchensink/internal/kitcontrol.h"

Kit_ControlPacket* _CreateControlPacket(Kit_ControlPacketType type, double value1) {
    Kit_ControlPacket *p = calloc(1, sizeof(Kit_ControlPacket));
    p->type = type;
    p->value1 = value1;
    return p;
}

void _FreeControlPacket(void *ptr) {
    Kit_ControlPacket *packet = ptr;
    free(packet);
}

static int reset_libass_track(Kit_Player *player) {
    AVCodecContext *scodec_ctx = player->scodec_ctx;

    if(scodec_ctx == NULL) {
        return 0;
    }

    // Flush libass track events
    ass_flush_events(player->ass_track);
    return 0;
}

void _HandleFlushCommand(Kit_Player *player, Kit_ControlPacket *packet) {
    if(player->abuffer != NULL) {
        if(SDL_LockMutex(player->amutex) == 0) {
            Kit_ClearBuffer((Kit_Buffer*)player->abuffer);
            SDL_UnlockMutex(player->amutex);
        }
    }
    if(player->vbuffer != NULL) {
        if(SDL_LockMutex(player->vmutex) == 0) {
            Kit_ClearBuffer((Kit_Buffer*)player->vbuffer);
            SDL_UnlockMutex(player->vmutex);
        }
    }
    if(player->sbuffer != NULL) {
        if(SDL_LockMutex(player->smutex) == 0) {
            Kit_ClearList((Kit_List*)player->sbuffer);
            SDL_UnlockMutex(player->smutex);
        }
    }
    reset_libass_track(player);
}

void _HandleSeekCommand(Kit_Player *player, Kit_ControlPacket *packet) {
    AVFormatContext *fmt_ctx = (AVFormatContext *)player->src->format_ctx;

    // Find and limit absolute position
    double seek = packet->value1;
    double duration = Kit_GetPlayerDuration(player);
    if(player->vclock_pos + seek <= 0) {
        seek = -player->vclock_pos;
    }
    if(player->vclock_pos + seek >= duration) {
        seek = duration - player->vclock_pos;
    }
    double absolute_pos = player->vclock_pos + seek;
    int64_t seek_target = absolute_pos * AV_TIME_BASE;

    // Seek to timestamp.
    avformat_seek_file(fmt_ctx, -1, INT64_MIN, seek_target, INT64_MAX, 0);
    if(player->vcodec_ctx != NULL)
        avcodec_flush_buffers(player->vcodec_ctx);
    if(player->acodec_ctx != NULL)
        avcodec_flush_buffers(player->acodec_ctx);

    // On first packet, set clock and current position
    player->seek_flag = 1;
}

