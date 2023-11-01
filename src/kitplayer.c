#include <assert.h>

#include "kitchensink/kitplayer.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/video/kitvideo.h"
#include "kitchensink/internal/audio/kitaudio.h"
#include "kitchensink/internal/subtitle/kitsubtitle.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/kitdecoderthread.h"
#include "kitchensink/internal/kitdemuxerthread.h"


Kit_Player* Kit_CreatePlayer(const Kit_Source *src,
                             int video_index,
                             int audio_index,
                             int subtitle_index,
                             int screen_w,
                             int screen_h) {
    assert(src != NULL);
    assert(screen_w >= 0);
    assert(screen_h >= 0);

    Kit_Player *player = NULL;
    Kit_Decoder *video_decoder = NULL;
    Kit_Decoder *audio_decoder = NULL;
    Kit_Decoder *subtitle_decoder = NULL;
    Kit_PacketBuffer *packet_buf = NULL;
    Kit_DecoderThread *video_thread = NULL;
    Kit_DecoderThread *audio_thread = NULL;
    Kit_DecoderThread *subtitle_thread = NULL;
    Kit_DemuxerThread *demux_thread = NULL;
    
    if(video_index < 0 && subtitle_index >= 0) {
        Kit_SetError("Subtitle stream selected without video stream");
        goto exit_0;
    }
    if((player = calloc(1, sizeof(Kit_Player))) == NULL) {
        Kit_SetError("Unable to allocate player");
        goto exit_0;
    }
    if((demux_thread = Kit_CreateDemuxerThread(src, video_index, audio_index, subtitle_index)) == NULL) {
        goto exit_1;
    }

    // Initialize audio decoder
    if(audio_index > -1) {
        if((audio_decoder = Kit_CreateAudioDecoder(src, audio_index)) == NULL) {
            goto exit_2;
        }
        packet_buf = Kit_GetDemuxerThreadPacketBuffer(demux_thread, KIT_AUDIO_INDEX);
        if((audio_thread = Kit_CreateDecoderThread(packet_buf, audio_decoder)) == NULL) {
            goto exit_2;
        }
    }

    // Initialize video decoder
    if(video_index > -1) {
        if((video_decoder = Kit_CreateVideoDecoder(src, video_index)) == NULL) {
            goto exit_2;
        }
        packet_buf = Kit_GetDemuxerThreadPacketBuffer(demux_thread, KIT_VIDEO_INDEX);
        if((video_thread = Kit_CreateDecoderThread(packet_buf, video_decoder)) == NULL) {
            goto exit_2;
        }
    }

    // Initialize subtitle decoder.
    if(subtitle_index > -1) {
        Kit_VideoOutputFormat output;
        Kit_GetVideoDecoderOutputFormat(player->decoders[KIT_VIDEO_INDEX], &output);
        subtitle_decoder = Kit_CreateSubtitleDecoder(
                src, subtitle_index, output.width, output.height, screen_w, screen_h);
        if(subtitle_decoder == NULL) {
            goto exit_2;
        }
        packet_buf = Kit_GetDemuxerThreadPacketBuffer(demux_thread, KIT_SUBTITLE_INDEX);
        if((subtitle_thread = Kit_CreateDecoderThread(packet_buf, subtitle_decoder)) == NULL) {
            goto exit_2;
        }
    }

    player->decoders[KIT_AUDIO_INDEX] = audio_decoder;
    player->decoders[KIT_VIDEO_INDEX] = video_decoder;
    player->decoders[KIT_SUBTITLE_INDEX] = subtitle_decoder;
    player->dec_threads[KIT_AUDIO_INDEX] = audio_thread;
    player->dec_threads[KIT_VIDEO_INDEX] = video_thread;
    player->dec_threads[KIT_SUBTITLE_INDEX] = subtitle_thread;
    player->demux_thread = demux_thread;
    player->src = src;
    return player;

exit_2:
    Kit_CloseDecoderThread(&audio_thread);
    Kit_CloseDecoderThread(&video_thread);
    Kit_CloseDecoderThread(&subtitle_thread);
    Kit_CloseDecoder(&audio_decoder);
    Kit_CloseDecoder(&video_decoder);
    Kit_CloseDecoder(&subtitle_decoder);
    Kit_CloseDemuxerThread(&demux_thread);
exit_1:
    free(player);
exit_0:
    return NULL;
}

void Kit_ClosePlayer(Kit_Player *player) {
    if(player == NULL)
        return;

    player->state = KIT_CLOSED;

    Kit_StopDemuxerThread(player->demux_thread);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_StopDecoderThread(player->dec_threads[i]);
        Kit_ClearDecoderBuffers(player->decoders[i]);
        Kit_SignalDecoder(player->decoders[i]);
        Kit_CloseDecoderThread((Kit_DecoderThread **)&player->dec_threads[i]);
    }
    Kit_CloseDemuxerThread((Kit_DemuxerThread**)&player->demux_thread);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_CloseDecoder((Kit_Decoder **)&player->decoders[i]);
    }
    memset(player, 0, sizeof(Kit_Player));
    free(player);
}

void Kit_SetPlayerScreenSize(Kit_Player *player, int w, int h) {
    assert(player != NULL);
    const Kit_Decoder *dec = player->decoders[KIT_SUBTITLE_INDEX];
    if(dec == NULL)
        return;
    Kit_SetSubtitleDecoderSize(dec, w, h);
}

int Kit_GetPlayerVideoStream(const Kit_Player *player) {
    if(player == NULL || !player->decoders[KIT_VIDEO_INDEX])
        return -1;
    return Kit_GetDecoderStreamIndex(player->decoders[KIT_VIDEO_INDEX]);
}

int Kit_GetPlayerAudioStream(const Kit_Player *player) {
    if(player == NULL || !player->decoders[KIT_AUDIO_INDEX])
        return -1;
    return Kit_GetDecoderStreamIndex(player->decoders[KIT_AUDIO_INDEX]);
}

int Kit_GetPlayerSubtitleStream(const Kit_Player *player) {
    if(player == NULL || !player->decoders[KIT_SUBTITLE_INDEX])
        return -1;
    return Kit_GetDecoderStreamIndex(player->decoders[KIT_SUBTITLE_INDEX]);
}

int Kit_GetPlayerVideoDataArea(Kit_Player *player, SDL_Texture *texture, SDL_Rect *area) {
    assert(player != NULL);

    Kit_Decoder *dec = player->decoders[KIT_VIDEO_INDEX];
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

    Kit_Decoder *dec = player->decoders[KIT_AUDIO_INDEX];
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

    const Kit_Decoder *sub_dec = player->decoders[KIT_SUBTITLE_INDEX];
    const Kit_Decoder *video_dec = player->decoders[KIT_VIDEO_INDEX];
    if(sub_dec == NULL || video_dec == NULL) {
        return 0;
    }

    // If paused, just return the current items
    if(player->state == KIT_PAUSED) {
        return Kit_GetSubtitleDecoderInfo(sub_dec, sources, targets, limit);
    }

    // If stopped, do nothing.
    if(player->state == KIT_STOPPED) {
        return 0;
    }

    // Refresh texture, then refresh rects and return number of items in the texture.
    Kit_GetSubtitleDecoderTexture(sub_dec, texture, video_dec->clock_pos);
    return Kit_GetSubtitleDecoderInfo(sub_dec, sources, targets, limit);
}

void Kit_GetPlayerInfo(const Kit_Player *player, Kit_PlayerInfo *info) {
    assert(player != NULL);
    assert(info != NULL);
    const Kit_Decoder *video_decoder = player->decoders[KIT_VIDEO_INDEX];
    const Kit_Decoder *audio_decoder = player->decoders[KIT_AUDIO_INDEX];
    const Kit_Decoder *subtitle_decoder = player->decoders[KIT_SUBTITLE_INDEX];
    Kit_GetDecoderCodecInfo(video_decoder, &info->video_codec);
    Kit_GetDecoderCodecInfo(audio_decoder, &info->audio_codec);
    Kit_GetDecoderCodecInfo(subtitle_decoder, &info->subtitle_codec);
    Kit_GetVideoDecoderOutputFormat(video_decoder, &info->video_format);
    Kit_GetAudioDecoderOutputFormat(audio_decoder, &info->audio_format);
    Kit_GetSubtitleDecoderOutputFormat(subtitle_decoder, &info->subtitle_format);
}

static void _ChangeClockSync(const Kit_Player *player, double delta) {
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_ChangeDecoderClockSync(player->decoders[i], delta);
    }
}

static void _StartThreads(const Kit_Player *player) {
    Kit_StartDemuxerThread(player->demux_thread);
    Kit_StartDecoderThread(player->dec_threads[KIT_VIDEO_INDEX], "Video decoder thread");
    Kit_StartDecoderThread(player->dec_threads[KIT_AUDIO_INDEX], "Audio decoder thread");
    Kit_StartDecoderThread(player->dec_threads[KIT_SUBTITLE_INDEX], "Subtitle decoder thread");
}

static void _StopThreads(const Kit_Player *player) {
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_StopDecoderThread(player->dec_threads[i]);
    }
    Kit_StopDemuxerThread(player->demux_thread);
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
            tmp = Kit_GetSystemTime() - player->pause_started;
            _ChangeClockSync(player, tmp);
            player->state = KIT_PLAYING;
            break;
        case KIT_STOPPED:
            _StartThreads(player);
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
            _StopThreads(player);
            break;
    }

}

void Kit_PlayerPause(Kit_Player *player) {
    assert(player != NULL);
    player->state = KIT_PAUSED;
    player->pause_started = Kit_GetSystemTime();
}

int Kit_PlayerSeek(Kit_Player *player, double seek_set) {
    assert(player != NULL);
    /*
    double position;
    double duration;
    int64_t seek_target;
    int flags = AVSEEK_FLAG_ANY;

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
        return 1;
    }

    // Clean old buffers and try to fill them with new data
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_ClearDecoderBuffers(player->decoders[i]);
    }
    _RunDecoder(player);

    // Try to get a precise seek position from the next audio/video frame
    // (depending on which one is used to sync)
    double precise_pts = -1.0F;
    if(player->decoders[KIT_VIDEO_INDEX] != NULL) {
        precise_pts = Kit_GetDecoderPTS(player->decoders[KIT_VIDEO_INDEX]);
    }
    else if(player->decoders[KIT_AUDIO_INDEX] != NULL) {
        precise_pts = Kit_GetDecoderPTS(player->decoders[KIT_AUDIO_INDEX]);
    }

    // If we got a legit looking value, set it as seek value. Otherwise use
    // the seek value we requested.
    if(precise_pts >= 0) {
        _ChangeClockSync(player, position - precise_pts);
    } else {
        _ChangeClockSync(player, position - seek_set);
    }*/

    return 0;
}

double Kit_GetPlayerDuration(const Kit_Player *player) {
    assert(player != NULL);
    const AVFormatContext *fmt_ctx = player->src->format_ctx;
    return (fmt_ctx->duration / AV_TIME_BASE);
}

double Kit_GetPlayerPosition(const Kit_Player *player) {
    assert(player != NULL);
    if(player->decoders[KIT_VIDEO_INDEX])
        return Kit_GetDecoderPTS(player->decoders[KIT_VIDEO_INDEX]);
    if(player->decoders[KIT_AUDIO_INDEX])
        return Kit_GetDecoderPTS(player->decoders[KIT_AUDIO_INDEX]);
    return 0;
}

#define IS_RATIONAL_DEFINED(rational) (rational.num > 0 && rational.den > 0)

int Kit_GetPlayerAspectRatio(const Kit_Player *player, int *num, int *den) {
    assert(player != NULL);
    const Kit_Decoder *decoder = player->decoders[KIT_VIDEO_INDEX];
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
    const AVStream *stream = decoder->stream;
    if(IS_RATIONAL_DEFINED(stream->sample_aspect_ratio)) {
        *num = stream->sample_aspect_ratio.num;
        *den = stream->sample_aspect_ratio.den;
        return 0;
    }

    // No data found anywhere, give up.
    Kit_SetError("Unable to find aspect ratio; no data from demuxer or codec");
    return 1;
}
