#include <assert.h>

#include <SDL.h>

#include "kitchensink/kitplayer.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/video/kitvideo.h"
#include "kitchensink/internal/audio/kitaudio.h"
#include "kitchensink/internal/subtitle/kitsubtitle.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/utils/kitlog.h"

enum DecoderIndex {
    KIT_VIDEO_DEC = 0,
    KIT_AUDIO_DEC,
    KIT_SUBTITLE_DEC,
    KIT_DEC_COUNT
};

// Return 0 if stream is good but nothing else to do for now
// Return -1 if there may still work to be done
// Return 1 if there was an error or stream end
static int _DemuxStream(const Kit_Player *player) {
    assert(player != NULL);
    AVFormatContext *format_ctx = player->src->format_ctx;

    // If any buffer is full, just stop here for now.
    // Since we don't know what kind of data is going to come out of av_read_frame, we really
    // want to make sure we are prepared for everything :)
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        Kit_Decoder *dec = player->decoders[i];
        if(dec == NULL)
            continue;
        if(!Kit_CanWriteDecoderInput(dec))
            return 0;
    }

    // Attempt to read frame. Just return here if it fails.
    AVPacket *packet = av_packet_alloc();
    if(av_read_frame(format_ctx, packet) < 0) {
        av_packet_free(&packet);
        return 1;
    }

    // Check if this is a packet we need to handle and pass it on
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        Kit_Decoder *dec = player->decoders[i];
        if(dec == NULL)
            continue;
        if(dec->stream_index == packet->stream_index) {
            Kit_WriteDecoderInput(player->decoders[i], packet);
            return -1;
        }
    }

    // We only get here if packet was not written to a decoder. IF that is the case,
    // disregard and free the packet.
    av_packet_free(&packet);
    return -1;
}

static bool _IsOutputEmpty(const Kit_Player *player) {
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        Kit_Decoder *dec = player->decoders[i];
        if(dec == NULL)
            continue;
        if(Kit_PeekDecoderOutput(dec))
            return false;
    }
    return true;
}

static int _RunDecoder(Kit_Player *player) {
    int got;
    int ret = 0;

    if(SDL_LockMutex(player->dec_lock) != 0) {
        return ret;
    }

    while((got = _DemuxStream(player)) == -1);
    if(got == 1 && _IsOutputEmpty(player)) {
        ret = 1;
        goto exit;
    }

    // Run decoders for a bit
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        while(Kit_RunDecoder(player->decoders[i]) == 1);
    }

exit:
    SDL_UnlockMutex(player->dec_lock);
    return ret;
}

static int _DecoderThread(void *ptr) {
    Kit_Player *player = ptr;
    bool is_running = true;
    bool is_playing = true;

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
            if(_RunDecoder(player) == 1) {
                player->state = KIT_STOPPED;
                continue;
            }
            SDL_Delay(2);
        }

        // Just idle while waiting for work.
        SDL_Delay(25);
    }

    return 0;
}

Kit_Player* Kit_CreatePlayer(const Kit_Source *src,
                             int video_stream_index,
                             int audio_stream_index,
                             int subtitle_stream_index,
                             int screen_w,
                             int screen_h) {
    assert(src != NULL);
    assert(screen_w >= 0);
    assert(screen_h >= 0);
    
    if(video_stream_index < 0 && subtitle_stream_index >= 0) {
        Kit_SetError("Subtitle stream selected without video stream");
        goto exit_0;
    }

    Kit_Player *player = calloc(1, sizeof(Kit_Player));
    if(player == NULL) {
        Kit_SetError("Unable to allocate player");
        goto exit_0;
    }

    // Initialize audio decoder
    player->decoders[KIT_AUDIO_DEC] = Kit_CreateAudioDecoder(src, audio_stream_index);
    if(player->decoders[KIT_AUDIO_DEC] == NULL && audio_stream_index >= 0) {
        goto exit_1;
    }

    // Initialize video decoder
    player->decoders[KIT_VIDEO_DEC] = Kit_CreateVideoDecoder(src, video_stream_index);
    if(player->decoders[KIT_VIDEO_DEC] == NULL && video_stream_index >= 0) {
        goto exit_2;
    }

    // Initialize subtitle decoder.
    Kit_OutputFormat output;
    Kit_GetDecoderOutputFormat(player->decoders[KIT_VIDEO_DEC], &output);
    player->decoders[KIT_SUBTITLE_DEC] = Kit_CreateSubtitleDecoder(
        src, subtitle_stream_index, output.width, output.height, screen_w, screen_h);
    if(player->decoders[KIT_SUBTITLE_DEC] == NULL && subtitle_stream_index >= 0) {
        goto exit_2;
    }

    // Decoder thread lock
    player->dec_lock = SDL_CreateMutex();
    if(player->dec_lock == NULL) {
        Kit_SetError("Unable to create a decoder thread lock mutex: %s", SDL_GetError());
        goto exit_2;
    }

    // Decoder thread
    player->dec_thread = SDL_CreateThread(_DecoderThread, "Kit Decoder Thread", player);
    if(player->dec_thread == NULL) {
        Kit_SetError("Unable to create a decoder thread: %s", SDL_GetError());
        goto exit_3;
    }

    player->src = src;
    return player;

exit_3:
    SDL_DestroyMutex(player->dec_lock);
exit_2:
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        Kit_CloseDecoder(player->decoders[i]);
    }
exit_1:
    free(player);
exit_0:
    return NULL;
}

void Kit_ClosePlayer(Kit_Player *player) {
    if(player == NULL) return;

    // Kill the decoder thread and mutex
    if(SDL_LockMutex(player->dec_lock) == 0) {
        player->state = KIT_CLOSED;
        SDL_UnlockMutex(player->dec_lock);
    }
    SDL_WaitThread(player->dec_thread, NULL);
    SDL_DestroyMutex(player->dec_lock);

    // Shutdown decoders
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        Kit_CloseDecoder(player->decoders[i]);
    }

    // Free the player structure itself
    free(player);
}

void Kit_SetPlayerScreenSize(Kit_Player *player, int w, int h) {
    assert(player != NULL);
    Kit_Decoder *dec = player->decoders[KIT_SUBTITLE_DEC];
    if(dec == NULL)
        return;
    Kit_SetSubtitleDecoderSize(dec, w, h);
}

int Kit_GetPlayerVideoStream(const Kit_Player *player) {
    assert(player != NULL);
    return Kit_GetDecoderStreamIndex(player->decoders[KIT_VIDEO_DEC]);
}

int Kit_GetPlayerAudioStream(const Kit_Player *player) {
    assert(player != NULL);
    return Kit_GetDecoderStreamIndex(player->decoders[KIT_AUDIO_DEC]);
}

int Kit_GetPlayerSubtitleStream(const Kit_Player *player) {
    assert(player != NULL);
    return Kit_GetDecoderStreamIndex(player->decoders[KIT_SUBTITLE_DEC]);
}

int Kit_GetPlayerVideoData(Kit_Player *player, SDL_Texture *texture) {
    assert(player != NULL);

    Kit_Decoder *dec = player->decoders[KIT_VIDEO_DEC];
    if(dec == NULL) {
        return 0;
    }

    // If paused or stopped, do nothing
    if(player->state == KIT_PAUSED) {
        return 0;
    }
    if(player->state == KIT_STOPPED) {
        return 0;
    }

    return Kit_GetVideoDecoderData(dec, texture);
}

int Kit_GetPlayerAudioData(Kit_Player *player, unsigned char *buffer, int length) {
    assert(player != NULL);
    assert(buffer != NULL);

    Kit_Decoder *dec = player->decoders[KIT_AUDIO_DEC];
    if(dec == NULL) {
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

    return Kit_GetAudioDecoderData(dec, buffer, length);
}

int Kit_GetPlayerSubtitleData(Kit_Player *player, SDL_Texture *texture, SDL_Rect *sources, SDL_Rect *targets, int limit) {
    assert(player != NULL);
    assert(texture != NULL);
    assert(sources != NULL);
    assert(targets != NULL);
    assert(limit >= 0);

    Kit_Decoder *dec = player->decoders[KIT_SUBTITLE_DEC];
    if(dec == NULL) {
        return 0;
    }

    // If paused, just return the current items
    if(player->state == KIT_PAUSED) {
        return Kit_GetSubtitleDecoderInfo(dec, texture, sources, targets, limit);
    }

    // If stopped, do nothing.
    if(player->state == KIT_STOPPED) {
        return 0;
    }

    // Refresh texture, then refresh rects and return number of items in the texture.
    Kit_GetSubtitleDecoderTexture(dec, texture);
    return Kit_GetSubtitleDecoderInfo(dec, texture, sources, targets, limit);
}

void Kit_GetPlayerInfo(const Kit_Player *player, Kit_PlayerInfo *info) {
    assert(player != NULL);
    assert(info != NULL);

    void *streams[] = {&info->video, &info->audio, &info->subtitle};
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        Kit_Decoder *dec = player->decoders[i];
        Kit_PlayerStreamInfo *stream = streams[i];
        Kit_GetDecoderCodecInfo(dec, &stream->codec);
        Kit_GetDecoderOutputFormat(dec, &stream->output);
    }
}

static void _SetClockSync(Kit_Player *player) {
    double sync = _GetSystemTime();
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        Kit_SetDecoderClockSync(player->decoders[i], sync);
    }
}

static void _ChangeClockSync(Kit_Player *player, double delta) {
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        Kit_ChangeDecoderClockSync(player->decoders[i], delta);
    }
}

Kit_PlayerState Kit_GetPlayerState(const Kit_Player *player) {
    assert(player != NULL);
    return player->state;
}

void Kit_PlayerPlay(Kit_Player *player) {
    assert(player != NULL);
    double tmp;
    if(SDL_LockMutex(player->dec_lock) == 0) {
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
        SDL_UnlockMutex(player->dec_lock);
    }
}

void Kit_PlayerStop(Kit_Player *player) {
    assert(player != NULL);
    if(SDL_LockMutex(player->dec_lock) == 0) {
        switch(player->state) {
            case KIT_STOPPED:
            case KIT_CLOSED:
                break;
            case KIT_PLAYING:
            case KIT_PAUSED:
                player->state = KIT_STOPPED;
                break;
        }
        SDL_UnlockMutex(player->dec_lock);
    }
}

void Kit_PlayerPause(Kit_Player *player) {
    assert(player != NULL);
    player->state = KIT_PAUSED;
    player->pause_started = _GetSystemTime();
}

int Kit_PlayerSeek(Kit_Player *player, double seek_set) {
    assert(player != NULL);
    double position;
    double duration;
    int64_t seek_target;
    int flags = AVSEEK_FLAG_ANY;

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
        if(seek_set < position) {
            flags |= AVSEEK_FLAG_BACKWARD;
        }
        if(avformat_seek_file(format_ctx, -1, 0, seek_target, seek_target, flags) < 0) {
            Kit_SetError("Unable to seek source");
            SDL_UnlockMutex(player->dec_lock);
            return 1;
        } else {
            _ChangeClockSync(player, position - seek_set);
            for(int i = 0; i < KIT_DEC_COUNT; i++) {
                Kit_ClearDecoderBuffers(player->decoders[i]);
            }
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

    if(player->decoders[KIT_VIDEO_DEC]) {
        return ((Kit_Decoder*)player->decoders[KIT_VIDEO_DEC])->clock_pos;
    }
    if(player->decoders[KIT_AUDIO_DEC]) {
        return ((Kit_Decoder*)player->decoders[KIT_AUDIO_DEC])->clock_pos;
    }
    return 0;
}
