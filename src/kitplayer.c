#include "kitchensink/kitplayer.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitlist.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/kitvideo.h"
#include "kitchensink/internal/kitaudio.h"
#include "kitchensink/internal/kitsubtitle.h"
#include "kitchensink/internal/kithelpers.h"
#include "kitchensink/internal/kitlog.h"

#include <SDL2/SDL.h>
#include <assert.h>

// Return 0 if stream is good but nothing else to do for now
// Return -1 if there is still work to be done
// Return 1 if there was an error or stream end
static int _DemuxStream(const Kit_Player *player) {
    assert(player != NULL);

    Kit_Decoder *video_dec = player->video_dec;
    Kit_Decoder *audio_dec = player->audio_dec;
    //Kit_Decoder *subtitle_dec = player->subtitle_dec;
    AVFormatContext *format_ctx = player->src->format_ctx;

    // If either buffer is full, just stop here for now.
    // Since we don't know what kind of data is going to come out of av_read_frame, we really
    // want to make sure we are prepared for everything :)
    if(video_dec != NULL) {
        if(!Kit_CanWriteDecoderInput(video_dec)) {
            return 0;
        }
    }
    if(audio_dec != NULL) {
        if(!Kit_CanWriteDecoderInput(audio_dec)) {
            return 0;
        }
    }

    // Attempt to read frame. Just return here if it fails.
    AVPacket *packet = av_packet_alloc();
    if(av_read_frame(format_ctx, packet) < 0) {
        av_packet_free(&packet);
        return 1;
    }

    // Check if this is a packet we need to handle and pass it on
    if(video_dec != NULL && packet->stream_index == video_dec->stream_index) {
        Kit_WriteDecoderInput(video_dec, packet);
    }
    else if(audio_dec != NULL && packet->stream_index == audio_dec->stream_index) {
        Kit_WriteDecoderInput(audio_dec, packet);
    }
    else {
        av_packet_free(&packet);
    }

    return -1;
}

static int _DecoderThread(void *ptr) {
    Kit_Player *player = ptr;
    bool is_running = true;
    bool is_playing = true;
    int ret;

    while(is_running) {
        if(player->state == KIT_CLOSED) {
            is_running = false;
            continue;
        }
        if(player->state == KIT_PLAYING) {
            is_playing = true;
        }
        while(is_running && is_playing) {
            if(player->state == KIT_CLOSED) {
                is_running = false;
                continue;
            }
            if(player->state == KIT_STOPPED) {
                is_playing = false;
                continue;
            }

            // Get more data from demuxer, decode. Wait a bit if there's no more work for now.
            if(SDL_LockMutex(player->dec_lock) == 0) {
                while((ret = _DemuxStream(player)) == -1);
                if(ret == 1) {
                    player->state = KIT_STOPPED;
                    continue;
                }

                // Run decoders for a bit
                while(Kit_RunDecoder(player->video_dec) == 1);
                while(Kit_RunDecoder(player->audio_dec) == 1);
                
                // Free decoder thread lock.
                SDL_UnlockMutex(player->dec_lock);
            }

            // We decoded as much as we could, sleep a bit.
            SDL_Delay(1);
        }

        // Just idle while waiting for work.
        SDL_Delay(10);
    }

    return 0;
}

Kit_Player* Kit_CreatePlayer(const Kit_Source *src) {
    assert(src != NULL);

    Kit_Player *player = calloc(1, sizeof(Kit_Player));
    if(player == NULL) {
        Kit_SetError("Unable to allocate player");
        return NULL;
    }

    // Initialize audio decoder
    player->audio_dec = Kit_CreateAudioDecoder(src, &player->aformat);
    if(player->audio_dec == NULL && src->audio_stream_index >= 0) {
        goto error;
    }

    // Initialize video decoder
    player->video_dec = Kit_CreateVideoDecoder(src, &player->vformat);
    if(player->video_dec == NULL && src->video_stream_index >= 0) {
        goto error;
    }

    // Initialize subtitle decoder
    player->subtitle_dec = Kit_CreateSubtitleDecoder(
        src, &player->sformat, player->vformat.width, player->vformat.height);
    if(player->subtitle_dec == NULL && src->subtitle_stream_index >= 0) {
        goto error;
    }

    // Decoder thread lock
    player->dec_lock = SDL_CreateMutex();
    if(player->dec_lock == NULL) {
        Kit_SetError("Unable to create a decoder thread: %s", SDL_GetError());
        goto error;
    }

    // Decoder thread
    player->dec_thread = SDL_CreateThread(_DecoderThread, "Kit Decoder Thread", player);
    if(player->dec_thread == NULL) {
        Kit_SetError("Unable to create a decoder thread lock mutex: %s", SDL_GetError());
        goto error;
    }

    player->src = src;
    return player;

error:
    Kit_ClosePlayer(player);
    return NULL;
}

void Kit_ClosePlayer(Kit_Player *player) {
    if(player == NULL) return;

    // Kill the decoder thread and lock mutex
    player->state = KIT_CLOSED;
    SDL_WaitThread(player->dec_thread, NULL);
    SDL_DestroyMutex(player->dec_lock);

    // Shutdown decoders
    Kit_CloseDecoder(player->audio_dec);
    Kit_CloseDecoder(player->video_dec);
    Kit_CloseDecoder(player->subtitle_dec);

    // Free the player structure itself
    free(player);
}

int Kit_GetVideoData(Kit_Player *player, SDL_Texture *texture) {
    assert(player != NULL);
    if(player->video_dec == NULL) {
        return 0;
    }

    // If paused or stopped, do nothing
    if(player->state == KIT_PAUSED) {
        return 0;
    }
    if(player->state == KIT_STOPPED) {
        return 0;
    }

    return Kit_GetVideoDecoderData(player->video_dec, texture);
}

int Kit_GetAudioData(Kit_Player *player, unsigned char *buffer, int length, int cur_buf_len) {
    assert(player != NULL);
    assert(buffer != NULL);
    if(player->audio_dec == NULL) {
        return 0;
    }

    // If asked for nothing, don't return anything either :P
    if(length == 0) {
        return 0;
    }

    // If paused or stopped, do nothing
    if(player->state == KIT_PAUSED) {
        return 0;
    }
    if(player->state == KIT_STOPPED) {
        return 0;
    }

    return Kit_GetAudioDecoderData(player->audio_dec, buffer, length);
}

int Kit_GetSubtitleData(Kit_Player *player, SDL_Renderer *renderer) {
    assert(player != NULL);
    if(player->subtitle_dec == NULL) {
        return 0;
    }

    // If paused or stopped, do nothing
    if(player->state == KIT_PAUSED) {
        return 0;
    }
    if(player->state == KIT_STOPPED) {
        return 0;
    }



    return 0;
}

void Kit_GetPlayerInfo(const Kit_Player *player, Kit_PlayerInfo *info) {
    assert(player != NULL);
    assert(info != NULL);

    Kit_Decoder *audio_dec = player->audio_dec;
    Kit_Decoder *video_dec = player->video_dec;
    Kit_Decoder *subtitle_dec = player->subtitle_dec;
    AVCodecContext *acodec_ctx = audio_dec ? audio_dec->codec_ctx : NULL;
    AVCodecContext *vcodec_ctx = video_dec ? video_dec->codec_ctx : NULL;
    AVCodecContext *scodec_ctx = subtitle_dec ? subtitle_dec->codec_ctx : NULL;

    // Reset everything to 0. We might not fill all fields.
    memset(info, 0, sizeof(Kit_PlayerInfo));

    if(acodec_ctx != NULL) {
        strncpy(info->acodec, acodec_ctx->codec->name, KIT_CODECMAX-1);
        strncpy(info->acodec_name, acodec_ctx->codec->long_name, KIT_CODECNAMEMAX-1);
        memcpy(&info->audio, &player->aformat, sizeof(Kit_AudioFormat));
    }
    if(vcodec_ctx != NULL) {
        strncpy(info->vcodec, vcodec_ctx->codec->name, KIT_CODECMAX-1);
        strncpy(info->vcodec_name, vcodec_ctx->codec->long_name, KIT_CODECNAMEMAX-1);
        memcpy(&info->video, &player->vformat, sizeof(Kit_VideoFormat));
    }
    if(scodec_ctx != NULL) {
        strncpy(info->scodec, scodec_ctx->codec->name, KIT_CODECMAX-1);
        strncpy(info->scodec_name, scodec_ctx->codec->long_name, KIT_CODECNAMEMAX-1);
        memcpy(&info->subtitle, &player->sformat, sizeof(Kit_SubtitleFormat));
    }
}

static void _SetClockSync(Kit_Player *player) {
    if(SDL_LockMutex(player->dec_lock) == 0) {
        double sync = _GetSystemTime();
        Kit_SetDecoderClockSync(player->video_dec, sync);
        Kit_SetDecoderClockSync(player->audio_dec, sync);
        Kit_SetDecoderClockSync(player->subtitle_dec, sync);
        SDL_UnlockMutex(player->dec_lock);
    }
}

static void _ChangeClockSync(Kit_Player *player, double delta) {
    if(SDL_LockMutex(player->dec_lock) == 0) {
        Kit_ChangeDecoderClockSync(player->video_dec, delta);
        Kit_ChangeDecoderClockSync(player->audio_dec, delta);
        Kit_ChangeDecoderClockSync(player->subtitle_dec, delta);
        SDL_UnlockMutex(player->dec_lock);
    }
}

Kit_PlayerState Kit_GetPlayerState(const Kit_Player *player) {
    assert(player != NULL);
    return player->state;
}

void Kit_PlayerPlay(Kit_Player *player) {
    assert(player != NULL);
    double tmp;
    switch(player->state) {
        case KIT_PLAYING:
        case KIT_CLOSED:
            break;
        case KIT_PAUSED:
            tmp = _GetSystemTime() - player->pause_started;
            _ChangeClockSync(player, tmp);
            player->state = KIT_PLAYING;
            break;
        case KIT_STOPPED:
            _SetClockSync(player);
            player->state = KIT_PLAYING;
            break;
    }
}

void Kit_PlayerStop(Kit_Player *player) {
    assert(player != NULL);
    switch(player->state) {
        case KIT_STOPPED:
        case KIT_CLOSED:
            break;
        case KIT_PLAYING:
        case KIT_PAUSED:
            player->state = KIT_STOPPED;
            break;
    }
}

void Kit_PlayerPause(Kit_Player *player) {
    assert(player != NULL);
    player->state = KIT_PAUSED;
    player->pause_started = _GetSystemTime();
}

int Kit_PlayerSeek(Kit_Player *player, double seek_set) {
    assert(player != NULL);
    double position, duration;
    int64_t seek_target;

    if(SDL_LockMutex(player->dec_lock) == 0) {
        duration = Kit_GetPlayerDuration(player);
        position = Kit_GetPlayerPosition(player);
        if(seek_set <= 0) {
            seek_set = 0;
        }
        if(seek_set >= duration) {
            seek_set = duration;
        }

        // Set source to timestamp
        AVFormatContext *format_ctx = player->src->format_ctx;
        seek_target = seek_set * AV_TIME_BASE;
        if(avformat_seek_file(format_ctx, -1, INT64_MIN, seek_target, seek_target, 0) < 0) {
            Kit_SetError("Unable to seek source");
            return 1;
        } else {
            _ChangeClockSync(player, position - seek_set);
            Kit_ClearDecoderBuffers(player->video_dec);
            Kit_ClearDecoderBuffers(player->audio_dec);
            Kit_ClearDecoderBuffers(player->subtitle_dec);
            while(_DemuxStream(player) == -1);
        }

        // That's it. Unlock and continue.
        SDL_UnlockMutex(player->dec_lock);
    }

    return 0;
}

double Kit_GetPlayerDuration(const Kit_Player *player) {
    assert(player != NULL);

    AVFormatContext *fmt_ctx = player->src->format_ctx;
    return (fmt_ctx->duration / AV_TIME_BASE);
}

double Kit_GetPlayerPosition(const Kit_Player *player) {
    assert(player != NULL);

    if(player->video_dec) {
        return ((Kit_Decoder*)player->video_dec)->clock_pos;
    }
    if(player->audio_dec) {
        return ((Kit_Decoder*)player->audio_dec)->clock_pos;
    }
    return 0;
}
