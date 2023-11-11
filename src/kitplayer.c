#include <assert.h>

#include "kitchensink/kitplayer.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/internal/video/kitvideo.h"
#include "kitchensink/internal/audio/kitaudio.h"
#include "kitchensink/internal/subtitle/kitsubtitle.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/kitdecoderthread.h"
#include "kitchensink/internal/kitdemuxerthread.h"
#include "kitchensink/internal/kittimer.h"

#include "SDL_timer.h"


struct Kit_Player {
    Kit_PlayerState state; ///< Playback state
    Kit_Decoder *decoders[3]; ///< Decoder contexts
    Kit_Demuxer *demuxer; ///< Demuxer context
    Kit_DecoderThread *dec_threads[3]; ///< Decoder threads
    Kit_DemuxerThread *demux_thread; ///< Demuxer thread
    Kit_Timer *sync_timer; ///< Sync timer for the decoders
    const Kit_Source *src; ///< Reference to Audio/Video source
    double pause_started; ///< Temporary flag for handling pauses
};

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
    Kit_Demuxer *demuxer = NULL;
    Kit_PacketBuffer *packet_buf = NULL;
    Kit_DecoderThread *video_thread = NULL;
    Kit_DecoderThread *audio_thread = NULL;
    Kit_DecoderThread *subtitle_thread = NULL;
    Kit_DemuxerThread *demux_thread = NULL;
    Kit_Timer *timer = NULL;
    Kit_Timer *audio_timer = NULL;
    Kit_Timer *video_timer = NULL;
    Kit_Timer *sub_timer = NULL;
    bool video_primary = video_index > -1;
    bool audio_primary = !video_primary && audio_index > -1;
    bool sub_primary = !audio_primary && subtitle_index > -1;
    
    if(video_index < 0 && subtitle_index >= 0) {
        Kit_SetError("Subtitle stream selected without video stream");
        goto exit_0;
    }
    if((player = calloc(1, sizeof(Kit_Player))) == NULL) {
        Kit_SetError("Unable to allocate player");
        goto exit_0;
    }
    if((timer = Kit_CreateTimer()) == NULL) {
        goto exit_1;
    }
    if((demuxer = Kit_CreateDemuxer(src, video_index, audio_index, subtitle_index)) == NULL) {
        goto exit_2;
    }
    if((demux_thread = Kit_CreateDemuxerThread(demuxer)) == NULL) {
        goto exit_3;
    }

    // Initialize audio decoder
    if(audio_index > -1) {
        if((audio_timer = Kit_CreateSecondaryTimer(timer, audio_primary)) == NULL)
            goto exit_3;
        if((audio_decoder = Kit_CreateAudioDecoder(src, audio_timer, audio_index)) == NULL)
            goto exit_3;
        packet_buf = Kit_GetDemuxerThreadPacketBuffer(demux_thread, KIT_AUDIO_INDEX);
        if((audio_thread = Kit_CreateDecoderThread(packet_buf, audio_decoder)) == NULL)
            goto exit_3;
    }

    // Initialize video decoder
    if(video_index > -1) {
        if((video_timer = Kit_CreateSecondaryTimer(timer, video_primary)) == NULL)
            goto exit_3;
        if((video_decoder = Kit_CreateVideoDecoder(src, video_timer, video_index)) == NULL)
            goto exit_3;
        packet_buf = Kit_GetDemuxerThreadPacketBuffer(demux_thread, KIT_VIDEO_INDEX);
        if((video_thread = Kit_CreateDecoderThread(packet_buf, video_decoder)) == NULL)
            goto exit_3;
    }

    // Initialize subtitle decoder.
    if(subtitle_index > -1) {
        Kit_VideoOutputFormat output;
        Kit_GetVideoDecoderOutputFormat(player->decoders[KIT_VIDEO_INDEX], &output);
        if((sub_timer = Kit_CreateSecondaryTimer(timer, sub_primary)) == NULL)
            goto exit_3;
        subtitle_decoder = Kit_CreateSubtitleDecoder(
                src, sub_timer, subtitle_index, output.width, output.height, screen_w, screen_h);
        if(subtitle_decoder == NULL)
            goto exit_3;
        packet_buf = Kit_GetDemuxerThreadPacketBuffer(demux_thread, KIT_SUBTITLE_INDEX);
        if((subtitle_thread = Kit_CreateDecoderThread(packet_buf, subtitle_decoder)) == NULL)
            goto exit_3;
    }

    player->decoders[KIT_AUDIO_INDEX] = audio_decoder;
    player->decoders[KIT_VIDEO_INDEX] = video_decoder;
    player->decoders[KIT_SUBTITLE_INDEX] = subtitle_decoder;
    player->dec_threads[KIT_AUDIO_INDEX] = audio_thread;
    player->dec_threads[KIT_VIDEO_INDEX] = video_thread;
    player->dec_threads[KIT_SUBTITLE_INDEX] = subtitle_thread;
    player->demuxer = demuxer;
    player->demux_thread = demux_thread;
    player->src = src;
    player->sync_timer = timer;
    return player;

exit_3:
    Kit_CloseDecoderThread(&audio_thread);
    Kit_CloseDecoderThread(&video_thread);
    Kit_CloseDecoderThread(&subtitle_thread);
    Kit_CloseDecoder(&audio_decoder);
    Kit_CloseDecoder(&video_decoder);
    Kit_CloseDecoder(&subtitle_decoder);
    Kit_CloseDemuxerThread(&demux_thread);
    Kit_CloseDemuxer(&demuxer);
    Kit_CloseTimer(&audio_timer);
    Kit_CloseTimer(&video_timer);
    Kit_CloseTimer(&sub_timer);
exit_2:
    Kit_CloseTimer(&timer);
exit_1:
    free(player);
exit_0:
    return NULL;
}

static void Kit_StartThreads(const Kit_Player *player) {
    Kit_StartDemuxerThread(player->demux_thread);
    Kit_StartDecoderThread(player->dec_threads[KIT_VIDEO_INDEX], "Video decoder thread");
    Kit_StartDecoderThread(player->dec_threads[KIT_AUDIO_INDEX], "Audio decoder thread");
    Kit_StartDecoderThread(player->dec_threads[KIT_SUBTITLE_INDEX], "Subtitle decoder thread");
}

static void Kit_StopThreads(const Kit_Player *player) {
    Kit_StopDemuxerThread(player->demux_thread);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_StopDecoderThread(player->dec_threads[i]);
    }
}

static void Kit_SignalAllBuffers(const Kit_Player *player) {
    Kit_SignalDemuxer(player->demuxer);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_SignalDecoder(player->decoders[i]);
    }
}

static void Kit_CloseThreads(Kit_Player *player) {
    Kit_CloseDemuxerThread(&player->demux_thread);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_CloseDecoderThread(&player->dec_threads[i]);
    }
}

static void Kit_FlushAllBuffers(const Kit_Player *player) {
    Kit_ClearDemuxerBuffers(player->demuxer);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_ClearDecoderBuffers(player->decoders[i]);
    }
}


void Kit_ClosePlayer(Kit_Player *player) {
    if(player == NULL)
        return;

    player->state = KIT_CLOSED;

    Kit_StopThreads(player);
    Kit_FlushAllBuffers(player);
    Kit_SignalAllBuffers(player);
    Kit_CloseThreads(player);
    Kit_CloseDemuxer(&player->demuxer);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_CloseDecoder(&player->decoders[i]);
    }
    Kit_CloseTimer(&player->sync_timer);
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

int Kit_GetPlayerVideoData(Kit_Player *player, SDL_Texture *texture, SDL_Rect *area) {
    assert(player != NULL);
    if(player->decoders[KIT_VIDEO_INDEX] == NULL)
        return 0;
    if(player->state == KIT_PAUSED || player->state == KIT_STOPPED)
        return 0;
    return Kit_GetVideoDecoderData(player->decoders[KIT_VIDEO_INDEX], texture, area);
}

int Kit_GetPlayerAudioData(Kit_Player *player, unsigned char *buffer, int length) {
    assert(player != NULL);
    assert(buffer != NULL);
    if(player->decoders[KIT_AUDIO_INDEX] == NULL)
        return 0;
    if(length == 0)
        return 0;
    if(player->state == KIT_PAUSED || player->state == KIT_STOPPED)
        return 0;
    return Kit_GetAudioDecoderData(player->decoders[KIT_AUDIO_INDEX], buffer, length);
}

int Kit_GetPlayerSubtitleData(Kit_Player *player, SDL_Texture *texture, SDL_Rect *sources, SDL_Rect *targets, int limit) {
    assert(player != NULL);
    assert(texture != NULL);
    assert(sources != NULL);
    assert(targets != NULL);
    assert(limit >= 0);

    const Kit_Decoder *sub_dec = player->decoders[KIT_SUBTITLE_INDEX];
    const Kit_Decoder *video_dec = player->decoders[KIT_VIDEO_INDEX];
    if(sub_dec == NULL || video_dec == NULL)
        return 0;
    if(player->state == KIT_PAUSED)  // If paused, just return the current items
        return Kit_GetSubtitleDecoderInfo(sub_dec, sources, targets, limit);
    if(player->state == KIT_STOPPED)  // If stopped, do nothing.
        return 0;
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
            Kit_AddTimerBase(player->sync_timer, tmp);
            player->state = KIT_PLAYING;
            break;
        case KIT_STOPPED:
            Kit_StartThreads(player);
            player->state = KIT_PLAYING;
            Kit_SetTimerBase(player->sync_timer);
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
            Kit_StopThreads(player);
            break;
    }
}

void Kit_PlayerPause(Kit_Player *player) {
    assert(player != NULL);
    if(player->state != KIT_PAUSED) {
        player->state = KIT_PAUSED;
        player->pause_started = Kit_GetSystemTime();
    }
}

int Kit_PlayerSeek(Kit_Player *player, double seek_set) {
    assert(player != NULL);
    double duration = Kit_GetPlayerDuration(player);
    if(seek_set <= 0)
        seek_set = 0;
    if(seek_set >= duration)
        seek_set = duration;
    Kit_SeekDemuxerThread(player->demux_thread, seek_set * AV_TIME_BASE);
    return 0;
}

double Kit_GetPlayerDuration(const Kit_Player *player) {
    assert(player != NULL);
    return Kit_GetSourceDuration(player->src);
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
