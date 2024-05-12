#include <assert.h>

#include "kitchensink/internal/audio/kitaudio.h"
#include "kitchensink/internal/kitdecoderthread.h"
#include "kitchensink/internal/kitdemuxerthread.h"
#include "kitchensink/internal/kittimer.h"
#include "kitchensink/internal/subtitle/kitsubtitle.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/video/kitvideo.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/kitplayer.h"

#include "SDL_timer.h"

struct Kit_Player {
    Kit_PlayerState state;             ///< Playback state
    Kit_Decoder *decoders[3];          ///< Decoder contexts
    Kit_Demuxer *demuxer;              ///< Demuxer context
    Kit_DecoderThread *dec_threads[3]; ///< Decoder threads
    Kit_DemuxerThread *demux_thread;   ///< Demuxer thread
    Kit_Timer *sync_timer;             ///< Sync timer for the decoders
    const Kit_Source *src;             ///< Reference to Audio/Video source
    double pause_started;              ///< Temporary flag for handling pauses
    int screen_w;                      ///< Width of the screen surface (for positioning subtitles)
    int screen_h;                      ///< Height of the screen surface (for positioning subtitles)
};

static bool Kit_InitializeAudioDecoder(
    const Kit_Source *src,
    const Kit_Timer *main_timer,
    const Kit_DemuxerThread *demux_thread,
    bool is_primary,
    int stream_index,
    Kit_Decoder **decoder,
    Kit_DecoderThread **thread
) {
    Kit_Timer *timer;
    Kit_PacketBuffer *packet_buffer;

    if((packet_buffer = Kit_GetDemuxerThreadPacketBuffer(demux_thread, KIT_AUDIO_INDEX)) == NULL)
        goto exit_0;
    if((timer = Kit_CreateSecondaryTimer(main_timer, is_primary)) == NULL)
        goto exit_0;
    if((*decoder = Kit_CreateAudioDecoder(src, timer, stream_index)) == NULL)
        goto exit_1;
    if((*thread = Kit_CreateDecoderThread(packet_buffer, *decoder)) == NULL)
        goto exit_2;

    return true;

exit_2:
    Kit_CloseDecoder(decoder);
exit_1:
    Kit_CloseTimer(&timer);
exit_0:
    return false;
}

static bool Kit_InitializeVideoDecoder(
    const Kit_Source *src,
    const Kit_Timer *main_timer,
    const Kit_DemuxerThread *demux_thread,
    bool is_primary,
    int stream_index,
    Kit_Decoder **decoder,
    Kit_DecoderThread **thread
) {
    Kit_Timer *timer;
    Kit_PacketBuffer *packet_buffer;

    if((packet_buffer = Kit_GetDemuxerThreadPacketBuffer(demux_thread, KIT_VIDEO_INDEX)) == NULL)
        goto exit_0;
    if((timer = Kit_CreateSecondaryTimer(main_timer, is_primary)) == NULL)
        goto exit_0;
    if((*decoder = Kit_CreateVideoDecoder(src, timer, stream_index)) == NULL)
        goto exit_1;
    if((*thread = Kit_CreateDecoderThread(packet_buffer, *decoder)) == NULL)
        goto exit_2;

    return true;

exit_2:
    Kit_CloseDecoder(decoder);
exit_1:
    Kit_CloseTimer(&timer);
exit_0:
    return false;
}

static bool Kit_InitializeSubtitleDecoder(
    const Kit_Source *src,
    const Kit_Timer *main_timer,
    const Kit_DemuxerThread *demux_thread,
    const Kit_Decoder *video_decoder,
    int stream_index,
    int screen_w,
    int screen_h,
    Kit_Decoder **decoder,
    Kit_DecoderThread **thread
) {
    Kit_Timer *timer;
    Kit_PacketBuffer *packet_buffer;
    Kit_VideoOutputFormat output;

    Kit_GetVideoDecoderOutputFormat(video_decoder, &output);
    if((packet_buffer = Kit_GetDemuxerThreadPacketBuffer(demux_thread, KIT_SUBTITLE_INDEX)) == NULL)
        goto exit_0;
    if((timer = Kit_CreateSecondaryTimer(main_timer, false)) == NULL)
        goto exit_0;
    if((*decoder = Kit_CreateSubtitleDecoder(src, timer, stream_index, output.width, output.height, screen_w, screen_h)
       ) == NULL)
        goto exit_1;
    if((*thread = Kit_CreateDecoderThread(packet_buffer, *decoder)) == NULL)
        goto exit_2;

    return true;

exit_2:
    Kit_CloseDecoder(decoder);
exit_1:
    Kit_CloseTimer(&timer);
exit_0:
    return false;
}

Kit_Player *Kit_CreatePlayer(
    const Kit_Source *src, int video_index, int audio_index, int subtitle_index, int screen_w, int screen_h
) {
    assert(src != NULL);
    assert(screen_w >= 0);
    assert(screen_h >= 0);

    Kit_Player *player = NULL;
    Kit_Decoder *video_decoder = NULL;
    Kit_Decoder *audio_decoder = NULL;
    Kit_Decoder *subtitle_decoder = NULL;
    Kit_Demuxer *demuxer = NULL;
    Kit_DecoderThread *video_thread = NULL;
    Kit_DecoderThread *audio_thread = NULL;
    Kit_DecoderThread *subtitle_thread = NULL;
    Kit_DemuxerThread *demux_thread = NULL;
    Kit_Timer *timer = NULL;
    bool video_primary = video_index > -1;
    bool audio_primary = !video_primary && audio_index > -1;

    if(video_index < 0 && subtitle_index >= 0) {
        Kit_SetError("Subtitle stream selected without video stream");
        goto exit_0;
    }
    if((player = calloc(1, sizeof(Kit_Player))) == NULL) {
        Kit_SetError("Unable to allocate player");
        goto exit_0;
    }
    if((timer = Kit_CreateTimer()) == NULL)
        goto exit_1;
    if((demuxer = Kit_CreateDemuxer(src, video_index, audio_index, subtitle_index)) == NULL)
        goto exit_2;
    if((demux_thread = Kit_CreateDemuxerThread(demuxer)) == NULL)
        goto exit_3;
    if(audio_index > -1) {
        if(!Kit_InitializeAudioDecoder(
               src, timer, demux_thread, audio_primary, audio_index, &audio_decoder, &audio_thread
           )) {
            goto exit_4;
        }
    }
    if(video_index > -1) {
        if(!Kit_InitializeVideoDecoder(
               src, timer, demux_thread, video_primary, video_index, &video_decoder, &video_thread
           )) {
            goto exit_5;
        }
    }
    if(subtitle_index > -1) {
        if(!Kit_InitializeSubtitleDecoder(
               src,
               timer,
               demux_thread,
               video_decoder,
               subtitle_index,
               screen_w,
               screen_h,
               &subtitle_decoder,
               &subtitle_thread
           )) {
            goto exit_6;
        }
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
    player->screen_w = screen_w;
    player->screen_h = screen_h;
    return player;

exit_6:
    Kit_CloseDecoderThread(&video_thread);
    Kit_CloseDecoder(&video_decoder);
exit_5:
    Kit_CloseDecoderThread(&audio_thread);
    Kit_CloseDecoder(&audio_decoder);
exit_4:
    Kit_CloseDemuxerThread(&demux_thread);
exit_3:
    Kit_CloseDemuxer(&demuxer);
exit_2:
    Kit_CloseTimer(&timer);
exit_1:
    free(player);
exit_0:
    return NULL;
}

static bool Kit_IsRunning(const Kit_Player *player) {
    if(Kit_IsDemuxerThreadAlive(player->demux_thread))
        return true;
    if(player->dec_threads[KIT_VIDEO_INDEX])
        if(Kit_IsDecoderThreadAlive(player->dec_threads[KIT_VIDEO_INDEX]))
            return true;
    if(player->dec_threads[KIT_AUDIO_INDEX])
        if(Kit_IsDecoderThreadAlive(player->dec_threads[KIT_AUDIO_INDEX]))
            return true;
    return Kit_GetPlayerPosition(player) < Kit_GetPlayerDuration(player);
}

static void Kit_HaltDecoder(Kit_Player *player, int index) {
    Kit_Decoder *decoder = player->decoders[index];
    Kit_DecoderThread *thread = player->dec_threads[index];
    Kit_StopDecoderThread(thread);
    Kit_ClearDecoderBuffers(decoder);
    Kit_SignalDecoder(decoder);
    Kit_CloseDecoderThread(&thread);
    Kit_CloseDecoder(&decoder);
}

static void Kit_StartThreadFor(const Kit_Player *player, Kit_BufferIndex index) {
    switch(index) {
        case KIT_VIDEO_INDEX:
            Kit_StartDecoderThread(player->dec_threads[KIT_VIDEO_INDEX], "Video decoder thread");
            return;
        case KIT_AUDIO_INDEX:
            Kit_StartDecoderThread(player->dec_threads[KIT_AUDIO_INDEX], "Audio decoder thread");
            return;
        case KIT_SUBTITLE_INDEX:
            Kit_StartDecoderThread(player->dec_threads[KIT_SUBTITLE_INDEX], "Subtitle decoder thread");
            return;
        default:
            return;
    }
}

static void Kit_StartThreads(const Kit_Player *player) {
    Kit_StartDemuxerThread(player->demux_thread);
    Kit_StartThreadFor(player, KIT_VIDEO_INDEX);
    Kit_StartThreadFor(player, KIT_AUDIO_INDEX);
    Kit_StartThreadFor(player, KIT_SUBTITLE_INDEX);
}

static void Kit_StopThreads(const Kit_Player *player) {
    Kit_StopDemuxerThread(player->demux_thread);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_StopDecoderThread(player->dec_threads[i]);
    }
}

static void Kit_WaitThreads(const Kit_Player *player) {
    Kit_WaitDemuxerThread(player->demux_thread);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_WaitDecoderThread(player->dec_threads[i]);
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

static void Kit_VerifyState(Kit_Player *player) {
    if(player->state == KIT_PAUSED || player->state == KIT_PLAYING) {
        if(!Kit_IsRunning(player)) {
            Kit_StopThreads(player);
            Kit_WaitThreads(player);
            player->state = KIT_STOPPED;
        }
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
    player->screen_w = w;
    player->screen_h = h;
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

int Kit_GetPlayerAudioData(Kit_Player *player, size_t backend_buffer_size, unsigned char *buffer, size_t length) {
    assert(player != NULL);
    assert(buffer != NULL);
    if(player->decoders[KIT_AUDIO_INDEX] == NULL)
        return 0;
    if(length == 0)
        return 0;
    if(player->state == KIT_PAUSED || player->state == KIT_STOPPED)
        return 0;
    return Kit_GetAudioDecoderData(player->decoders[KIT_AUDIO_INDEX], backend_buffer_size, buffer, length);
}

int Kit_GetPlayerSubtitleData(
    Kit_Player *player, SDL_Texture *texture, SDL_Rect *sources, SDL_Rect *targets, int limit
) {
    assert(player != NULL);
    assert(texture != NULL);
    assert(sources != NULL);
    assert(targets != NULL);
    assert(limit >= 0);

    const Kit_Decoder *sub_dec = player->decoders[KIT_SUBTITLE_INDEX];
    if(sub_dec == NULL)
        return 0;
    if(player->state == KIT_PAUSED) // If paused, just return the current items
        return Kit_GetSubtitleDecoderInfo(sub_dec, sources, targets, limit);
    if(player->state == KIT_STOPPED) // If stopped, do nothing.
        return 0;
    Kit_GetSubtitleDecoderTexture(sub_dec, texture, Kit_GetTimerElapsed(player->sync_timer));
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

Kit_PlayerState Kit_GetPlayerState(Kit_Player *player) {
    assert(player != NULL);
    Kit_VerifyState(player);
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
    if(player->state == KIT_STOPPED || player->state == KIT_CLOSED)
        return 1;
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
    double pos = Kit_GetTimerElapsed(player->sync_timer);
    double dur = Kit_GetPlayerDuration(player);
    return pos >= dur ? dur : pos;
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

static void Kit_IsStreamPrimary(const Kit_Player *player, bool *video_primary, bool *audio_primary) {
    Kit_Decoder *video_decoder = player->decoders[KIT_VIDEO_INDEX];
    Kit_Decoder *audio_decoder = player->decoders[KIT_AUDIO_INDEX];
    *video_primary = video_decoder && video_decoder->stream->index > -1;
    *audio_primary = audio_decoder && !*video_primary && audio_decoder->stream->index > -1;
}

int Kit_SetPlayerStream(Kit_Player *player, const Kit_StreamType type, int index) {
    Kit_Decoder *new_decoder = NULL;
    Kit_DecoderThread *new_thread = NULL;
    Kit_BufferIndex buffer_index;
    bool video_primary, audio_primary;

    if(index < 0) {
        Kit_SetError("Invalid stream index");
        return -1;
    }

    // Figure out which stream is currently the primary one. This stream is allowed to modify the sync clock.
    Kit_IsStreamPrimary(player, &video_primary, &audio_primary);

    // First, attempt to start up a new decoder instance. If this fails, we don't want to disturb the
    // currently running decoder.
    switch(type) {
        case KIT_STREAMTYPE_AUDIO:
            buffer_index = KIT_AUDIO_INDEX;
            if(!Kit_InitializeAudioDecoder(
                   player->src,
                   player->sync_timer,
                   player->demux_thread,
                   audio_primary,
                   index,
                   &new_decoder,
                   &new_thread
               ))
                goto error_1;
            break;
        case KIT_STREAMTYPE_VIDEO:
            buffer_index = KIT_VIDEO_INDEX;
            if(!Kit_InitializeVideoDecoder(
                   player->src,
                   player->sync_timer,
                   player->demux_thread,
                   video_primary,
                   index,
                   &new_decoder,
                   &new_thread
               ))
                goto error_1;
            break;
        case KIT_STREAMTYPE_SUBTITLE:
            buffer_index = KIT_SUBTITLE_INDEX;
            if(!Kit_InitializeSubtitleDecoder(
                   player->src,
                   player->sync_timer,
                   player->demux_thread,
                   player->decoders[KIT_VIDEO_INDEX],
                   index,
                   player->screen_w,
                   player->screen_h,
                   &new_decoder,
                   &new_thread
               ))
                goto error_1;
            break;
        default:
            return -1;
    }

    // If we have a good new decoder, stop the old thread and decoder.
    Kit_HaltDecoder(player, buffer_index);

    // Switch demuxer to track the new stream index. This will also clear the packet buffer, so that the decoder
    // will no longer get packets from the old stream.
    Kit_SetDemuxerStreamIndex(player->demuxer, buffer_index, index);

    // Set the new decoder and thread, and spin up the thread if we were already playing.
    player->decoders[buffer_index] = new_decoder;
    player->dec_threads[buffer_index] = new_thread;
    if(player->state == KIT_PLAYING || player->state == KIT_PAUSED)
        Kit_StartThreadFor(player, buffer_index);

    // Et voila!
    return 0;

error_1:
    Kit_CloseDecoder(&new_decoder);
    Kit_CloseDecoderThread(&new_thread);
    return -1;
}

int Kit_GetPlayerStream(const Kit_Player *player, const Kit_StreamType type) {
    Kit_Decoder *dec = NULL;
    switch(type) {
        case KIT_STREAMTYPE_AUDIO:
            dec = player->decoders[KIT_AUDIO_INDEX];
            break;
        case KIT_STREAMTYPE_VIDEO:
            dec = player->decoders[KIT_VIDEO_INDEX];
            break;
        case KIT_STREAMTYPE_SUBTITLE:
            dec = player->decoders[KIT_SUBTITLE_INDEX];
            break;
        default:
            dec = NULL;
    }
    return dec ? dec->stream->index : -1;
}