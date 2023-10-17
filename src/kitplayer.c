#include <assert.h>

#include <SDL.h>

#include "kitchensink/kitplayer.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/video/kitvideo.h"
#include "kitchensink/internal/audio/kitaudio.h"
#include "kitchensink/internal/subtitle/kitsubtitle.h"
#include "kitchensink/internal/utils/kithelpers.h"

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
    const Kit_Decoder *dec = NULL;

    // If any buffer is full, just stop here for now.
    // Since we don't know what kind of data is going to come out of av_read_frame, we really
    // want to make sure we are prepared for everything :)
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        dec = player->decoders[i];
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
        dec = player->decoders[i];
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
    const Kit_Decoder *dec = NULL;
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        dec = player->decoders[i];
        if(dec == NULL)
            continue;
        if(Kit_PeekDecoderOutput(dec))
            return false;
    }
    return true;
}

static int _RunDecoder(const Kit_Player *player) {
    int got;
    bool has_room = true;
    const Kit_Decoder *dec = NULL;

    do {
        while((got = _DemuxStream(player)) == -1);
        if(got == 1 && _IsOutputEmpty(player)) {
            return 1;
        }

        for(int i = 0; i < KIT_DEC_COUNT; i++) {
            while(Kit_RunDecoder(player->decoders[i]) == 1);
        }

        // If there is no room in any decoder input, just stop here since it likely means that
        // at least some decoder output is full.
        for(int i = 0; i < KIT_DEC_COUNT; i++) {
            dec = player->decoders[i];
            if(dec == NULL)
                continue;
            if(!Kit_CanWriteDecoderInput(dec) || got == 1) {
                has_room = false;
                break;
            }
        }
    } while(has_room);

    return 0;
}

static void _TryWork(Kit_Player *player) {
    /**
     * \brief Run the decoders and demuxer as long as there is work. Returns when playback stops.
     */
    while(player->state == KIT_PLAYING || player->state == KIT_PAUSED) {
        // Grab the decoder lock, and run demuxer & decoders for a bit.
        if(SDL_LockMutex(player->dec_lock) == 0) {
            if(_RunDecoder(player) == 1) {
                player->state = KIT_STOPPED;
            }
            SDL_UnlockMutex(player->dec_lock);
        }

        // Delay to make sure this thread does not hog all cpu
        SDL_Delay(2);
    }
}

static int _DecoderThread(void *ptr) {
    /**
     * \brief Decoder thread main, which runs as long as the player exists.
     */
    Kit_Player *player = ptr;

    while(true) {
        if(player->state == KIT_CLOSED) {
            break;
        }
        
        // This will block as long as there is something to demux/decode
        // Returns when playback stops.
        _TryWork(player);

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
        goto EXIT_0;
    }

    Kit_Player *player = calloc(1, sizeof(Kit_Player));
    if(player == NULL) {
        Kit_SetError("Unable to allocate player");
        goto EXIT_0;
    }

    // Initialize audio decoder
    player->decoders[KIT_AUDIO_DEC] = Kit_CreateAudioDecoder(src, audio_stream_index);
    if(player->decoders[KIT_AUDIO_DEC] == NULL && audio_stream_index >= 0) {
        goto EXIT_1;
    }

    // Initialize video decoder
    player->decoders[KIT_VIDEO_DEC] = Kit_CreateVideoDecoder(src, video_stream_index);
    if(player->decoders[KIT_VIDEO_DEC] == NULL && video_stream_index >= 0) {
        goto EXIT_2;
    }

    // Initialize subtitle decoder.
    Kit_OutputFormat output;
    Kit_GetDecoderOutputFormat(player->decoders[KIT_VIDEO_DEC], &output);
    player->decoders[KIT_SUBTITLE_DEC] = Kit_CreateSubtitleDecoder(
        src, subtitle_stream_index, output.width, output.height, screen_w, screen_h);
    if(player->decoders[KIT_SUBTITLE_DEC] == NULL && subtitle_stream_index >= 0) {
        goto EXIT_2;
    }

    // Decoder thread lock
    player->dec_lock = SDL_CreateMutex();
    if(player->dec_lock == NULL) {
        Kit_SetError("Unable to create a decoder thread lock mutex: %s", SDL_GetError());
        goto EXIT_2;
    }

    // Decoder thread
    player->dec_thread = SDL_CreateThread(_DecoderThread, "Kit Decoder Thread", player);
    if(player->dec_thread == NULL) {
        Kit_SetError("Unable to create a decoder thread: %s", SDL_GetError());
        goto EXIT_3;
    }

    player->src = src;
    return player;

EXIT_3:
    SDL_DestroyMutex(player->dec_lock);
EXIT_2:
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        Kit_CloseDecoder(player->decoders[i]);
    }
EXIT_1:
    free(player);
EXIT_0:
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
    const Kit_Decoder *dec = player->decoders[KIT_SUBTITLE_DEC];
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

int Kit_GetPlayerVideoDataArea(Kit_Player *player, SDL_Texture *texture, SDL_Rect *area) {
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

    return Kit_GetVideoDecoderData(dec, texture, area);
}

int Kit_GetPlayerVideoData(Kit_Player *player, SDL_Texture *texture) {
    assert(player != NULL);
    SDL_Rect area;
    return Kit_GetPlayerVideoDataArea(player, texture, &area);
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

    const Kit_Decoder *sub_dec = player->decoders[KIT_SUBTITLE_DEC];
    const Kit_Decoder *video_dec = player->decoders[KIT_VIDEO_DEC];
    if(sub_dec == NULL || video_dec == NULL) {
        return 0;
    }

    // If paused, just return the current items
    if(player->state == KIT_PAUSED) {
        return Kit_GetSubtitleDecoderInfo(sub_dec, texture, sources, targets, limit);
    }

    // If stopped, do nothing.
    if(player->state == KIT_STOPPED) {
        return 0;
    }

    // Refresh texture, then refresh rects and return number of items in the texture.
    Kit_GetSubtitleDecoderTexture(sub_dec, texture, video_dec->clock_pos);
    return Kit_GetSubtitleDecoderInfo(sub_dec, texture, sources, targets, limit);
}

void Kit_GetPlayerInfo(const Kit_Player *player, Kit_PlayerInfo *info) {
    assert(player != NULL);
    assert(info != NULL);
    const Kit_Decoder *dec = NULL;
    Kit_PlayerStreamInfo *streams[] = {&info->video, &info->audio, &info->subtitle};

    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        dec = player->decoders[i];
        Kit_GetDecoderCodecInfo(dec, &streams[i]->codec);
        Kit_GetDecoderOutputFormat(dec, &streams[i]->output);
    }
}

static void _SetClockSync(const Kit_Player *player) {
    double sync = _GetSystemTime();
    for(int i = 0; i < KIT_DEC_COUNT; i++) {
        Kit_SetDecoderClockSync(player->decoders[i], sync);
    }
}

static void _ChangeClockSync(const Kit_Player *player, double delta) {
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
                _RunDecoder(player); // Fill some buffers before starting playback
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
                for(int i = 0; i < KIT_DEC_COUNT; i++) {
                    Kit_ClearDecoderBuffers(player->decoders[i]);
                }
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

        // First, tell ffmpeg to seek stream. If not capable, stop here.
        // Failure here probably means that stream is unseekable someway, eg. streamed media
        if(avformat_seek_file(format_ctx, -1, seek_target, seek_target, INT64_MAX, flags) < 0) {
            Kit_SetError("Unable to seek source");
            SDL_UnlockMutex(player->dec_lock);
            return 1;
        }

        // Clean old buffers and try to fill them with new data
        for(int i = 0; i < KIT_DEC_COUNT; i++) {
            Kit_ClearDecoderBuffers(player->decoders[i]);
        }
        _RunDecoder(player);

        // Try to get a precise seek position from the next audio/video frame
        // (depending on which one is used to sync)
        double precise_pts = -1.0F;
        if(player->decoders[KIT_VIDEO_DEC] != NULL) {
            precise_pts = Kit_GetVideoDecoderPTS(player->decoders[KIT_VIDEO_DEC]);
        }
        else if(player->decoders[KIT_AUDIO_DEC] != NULL) {
            precise_pts = Kit_GetAudioDecoderPTS(player->decoders[KIT_AUDIO_DEC]);
        }

        // If we got a legit looking value, set it as seek value. Otherwise use
        // the seek value we requested.
        if(precise_pts >= 0) {
            _ChangeClockSync(player, position - precise_pts);
        } else {
            _ChangeClockSync(player, position - seek_set);
        }

        // That's it. Unlock and continue.
        SDL_UnlockMutex(player->dec_lock);
    }

    return 0;
}

double Kit_GetPlayerDuration(const Kit_Player *player) {
    assert(player != NULL);

    const AVFormatContext *fmt_ctx = player->src->format_ctx;
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

#define IS_RATIONAL_DEFINED(rational) (rational.num > 0 && rational.den > 0)

int Kit_GetPlayerAspectRatio(const Kit_Player *player, int *num, int *den) {
    assert(player != NULL);
    const Kit_Decoder *decoder = player->decoders[KIT_VIDEO_DEC];
    if(!decoder) {
        Kit_SetError("Unable to find aspect ratio; no video stream selected");
        return 1;
    }

    // First off, try to get the aspect ratio of the currently showing frame.
    // This may change frame-to-frame.
    if(IS_RATIONAL_DEFINED(decoder->aspect_ratio)) {
        *num = decoder->aspect_ratio.num;
        *den = decoder->aspect_ratio.den;
        return 0;
    }

    // Then, try to find aspect ratio from the decoder itself
    const AVCodecContext *codec_ctx = decoder->codec_ctx;
    if(IS_RATIONAL_DEFINED(codec_ctx->sample_aspect_ratio)) {
        *num = codec_ctx->sample_aspect_ratio.num;
        *den = codec_ctx->sample_aspect_ratio.den;
        return 0;
    }

    // Then, try the stream (demuxer) data
    const AVFormatContext *fmt_ctx = player->src->format_ctx;
    const AVStream *stream = fmt_ctx->streams[decoder->stream_index];
    if(IS_RATIONAL_DEFINED(stream->sample_aspect_ratio)) {
        *num = stream->sample_aspect_ratio.num;
        *den = stream->sample_aspect_ratio.den;
        return 0;
    }

    // No data found anywhere, give up.
    Kit_SetError("Unable to find aspect ratio; no data from demuxer or codec");
    return 1;
}
