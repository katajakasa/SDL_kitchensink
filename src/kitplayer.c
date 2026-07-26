#include <SDL_atomic.h>
#include <SDL_timer.h>
#include <SDL_version.h>
#include <assert.h>

#include "kitchensink2/internal/audio/kitaudio.h"
#include "kitchensink2/internal/kitdecoderthread.h"
#include "kitchensink2/internal/kitdemuxerthread.h"
#include "kitchensink2/internal/kittimer.h"
#include "kitchensink2/internal/subtitle/kitsubtitle.h"
#include "kitchensink2/internal/utils/kithelpers.h"
#include "kitchensink2/internal/video/kitvideo.h"
#include "kitchensink2/kiterror.h"
#include "kitchensink2/kitplayer.h"

/**
 * Locking rules:
 * - Control lock serializes lifecycle operations (play/stop/pause/seek, stream switch, state check).
 * - Decoder control locks guard the decoder-threads against concurrent stream switching
 * - Lock order is main control lock first, then a decoder control lock. Slot critical sections must stay short.
 */
struct Kit_Player {
    SDL_atomic_t state;                ///< Playback state
    Kit_Decoder *decoders[3];          ///< Decoder contexts
    Kit_Demuxer *demuxer;              ///< Demuxer context
    Kit_DecoderThread *dec_threads[3]; ///< Decoder threads
    Kit_DemuxerThread *demux_thread;   ///< Demuxer thread
    Kit_Timer *sync_timer;             ///< Sync timer for the decoders
    Kit_VideoFormatRequest video_req;  ///< Original video format request
    Kit_AudioFormatRequest audio_req;  ///< Original audio format request
    const Kit_Source *src;             ///< Reference to Audio/Video source
    int screen_w;                      ///< Width of the screen surface (for positioning subtitles)
    int screen_h;                      ///< Height of the screen surface (for positioning subtitles)
    SDL_mutex *control_lock;           ///< Serializes lifecycle operations
    SDL_mutex *decoder_ctrl_locks[3];  ///< Guard decoders against concurrent getters
};

static Kit_PlayerState Kit_GetState(const Kit_Player *player) {
    return SDL_AtomicGet((SDL_atomic_t *)&player->state);
}

static void Kit_SetState(Kit_Player *player, Kit_PlayerState state) {
    SDL_AtomicSet(&player->state, state);
}

static void Kit_LockDecoderCtrl(const Kit_Player *player, int index) {
    SDL_LockMutex(player->decoder_ctrl_locks[index]);
}

static void Kit_UnlockDecoderCtrl(const Kit_Player *player, int index) {
    SDL_UnlockMutex(player->decoder_ctrl_locks[index]);
}

static bool Kit_InitializeAudioDecoder(
    const Kit_Source *src,
    const Kit_Timer *main_timer,
    const Kit_DemuxerThread *demux_thread,
    const Kit_AudioFormatRequest *format_request,
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
    if((*decoder = Kit_CreateAudioDecoder(src, format_request, timer, stream_index)) == NULL)
        goto exit_0;
    if((*thread = Kit_CreateDecoderThread(packet_buffer, *decoder)) == NULL)
        goto exit_1;

    return true;

exit_1:
    Kit_CloseDecoder(decoder);
exit_0:
    return false;
}

static bool Kit_InitializeVideoDecoder(
    const Kit_Source *src,
    const Kit_Timer *main_timer,
    const Kit_DemuxerThread *demux_thread,
    const Kit_VideoFormatRequest *format_request,
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
    if((*decoder = Kit_CreateVideoDecoder(src, format_request, timer, stream_index)) == NULL)
        goto exit_0;
    if((*thread = Kit_CreateDecoderThread(packet_buffer, *decoder)) == NULL)
        goto exit_1;

    return true;

exit_1:
    Kit_CloseDecoder(decoder);
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
    if((*decoder =
            Kit_CreateSubtitleDecoder(src, timer, stream_index, output.width, output.height, screen_w, screen_h)) ==
       NULL)
        goto exit_0;
    if((*thread = Kit_CreateDecoderThread(packet_buffer, *decoder)) == NULL)
        goto exit_1;

    return true;

exit_1:
    Kit_CloseDecoder(decoder);
exit_0:
    return false;
}

Kit_Player *Kit_CreatePlayer(
    const Kit_Source *src,
    const int video_stream_index,
    const int audio_stream_index,
    const int subtitle_stream_index,
    const Kit_VideoFormatRequest *video_format_request,
    const Kit_AudioFormatRequest *audio_format_request,
    const int screen_w,
    const int screen_h
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
    Kit_VideoFormatRequest tmp_video_format_request;
    Kit_AudioFormatRequest tmp_audio_format_request;
    const bool video_primary = video_stream_index > -1;
    const bool audio_primary = !video_primary && audio_stream_index > -1;

    if(video_format_request == NULL) {
        Kit_ResetVideoFormatRequest(&tmp_video_format_request);
    } else {
        tmp_video_format_request = *video_format_request;
    }
    if(audio_format_request == NULL) {
        Kit_ResetAudioFormatRequest(&tmp_audio_format_request);
    } else {
        tmp_audio_format_request = *audio_format_request;
    }
    if(video_stream_index < 0 && subtitle_stream_index >= 0) {
        Kit_SetError("Subtitle stream selected without video stream");
        goto exit_0;
    }
    if((player = calloc(1, sizeof(Kit_Player))) == NULL) {
        Kit_SetError("Unable to allocate player");
        goto exit_0;
    }
    if((player->control_lock = SDL_CreateMutex()) == NULL) {
        Kit_SetError("Unable to allocate player control lock: %s", SDL_GetError());
        goto exit_1;
    }
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        if((player->decoder_ctrl_locks[i] = SDL_CreateMutex()) == NULL) {
            Kit_SetError("Unable to allocate player decoder control lock: %s", SDL_GetError());
            goto exit_1;
        }
    }
    if((timer = Kit_CreateTimer()) == NULL)
        goto exit_1;
    if((demuxer = Kit_CreateDemuxer(src, video_stream_index, audio_stream_index, subtitle_stream_index)) == NULL)
        goto exit_2;
    if((demux_thread = Kit_CreateDemuxerThread(demuxer, timer)) == NULL)
        goto exit_3;
    if(audio_stream_index > -1) {
        if(!Kit_InitializeAudioDecoder(
               src,
               timer,
               demux_thread,
               &tmp_audio_format_request,
               audio_primary,
               audio_stream_index,
               &audio_decoder,
               &audio_thread
           )) {
            goto exit_4;
        }
    }
    if(video_stream_index > -1) {
        if(!Kit_InitializeVideoDecoder(
               src,
               timer,
               demux_thread,
               &tmp_video_format_request,
               video_primary,
               video_stream_index,
               &video_decoder,
               &video_thread
           )) {
            goto exit_5;
        }
    }
    if(subtitle_stream_index > -1) {
        if(!Kit_InitializeSubtitleDecoder(
               src,
               timer,
               demux_thread,
               video_decoder,
               subtitle_stream_index,
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
    player->video_req = tmp_video_format_request;
    player->audio_req = tmp_audio_format_request;
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
    SDL_DestroyMutex(player->control_lock);
    for(int i = 0; i < KIT_INDEX_COUNT; i++)
        SDL_DestroyMutex(player->decoder_ctrl_locks[i]);
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

/**
 * Detach a decoder and its thread from the player under the decoder control lock, so that getters running on
 * other threads can no longer reach them.
 */
static void Kit_StealDecoder(Kit_Player *player, int index, Kit_Decoder **decoder, Kit_DecoderThread **thread) {
    Kit_LockDecoderCtrl(player, index);
    *decoder = player->decoders[index];
    *thread = player->dec_threads[index];
    player->decoders[index] = NULL;
    player->dec_threads[index] = NULL;
    Kit_UnlockDecoderCtrl(player, index);
}

/**
 *  Stop and free a decoder detached with Kit_StealDecoder().
 */
static void Kit_HaltDecoder(Kit_Decoder *decoder, Kit_DecoderThread *thread) {
    Kit_StopDecoderThread(thread);
    Kit_AbortDecoder(decoder);
    Kit_CloseDecoderThread(&thread);
    Kit_CloseDecoder(&decoder);
}

static void Kit_StartThreadFor(const Kit_Player *player, Kit_BufferIndex index) {
    static const char *const thread_names[KIT_INDEX_COUNT] = {
        [KIT_VIDEO_INDEX] = "Video decoder thread",
        [KIT_AUDIO_INDEX] = "Audio decoder thread",
        [KIT_SUBTITLE_INDEX] = "Subtitle decoder thread",
    };
    if(index < 0 || index >= KIT_INDEX_COUNT)
        return;
    Kit_StartDecoderThread(player->dec_threads[index], thread_names[index]);
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

static void Kit_AbortAllBuffers(const Kit_Player *player) {
    Kit_AbortDemuxer(player->demuxer);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_AbortDecoder(player->decoders[i]);
    }
}

static void Kit_FlushAllBuffers(const Kit_Player *player) {
    Kit_ClearDemuxerBuffers(player->demuxer);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_ClearDecoderBuffers(player->decoders[i]);
    }
}

static void Kit_VerifyState(Kit_Player *player) {
    const Kit_PlayerState state = Kit_GetState(player);
    if(state == KIT_PAUSED || state == KIT_PLAYING) {
        if(!Kit_IsRunning(player)) {
            Kit_StopThreads(player);
            Kit_AbortAllBuffers(player);
            Kit_WaitThreads(player);
            Kit_FlushAllBuffers(player);
            Kit_SetState(player, KIT_STOPPED);
        }
    }
}

void Kit_ClosePlayer(Kit_Player *player) {
    Kit_Decoder *decoders[KIT_INDEX_COUNT];
    Kit_DecoderThread *dec_threads[KIT_INDEX_COUNT];
    if(player == NULL)
        return;

    Kit_SetState(player, KIT_CLOSED);

    SDL_LockMutex(player->control_lock);

    // Detach the decoders under their locks first, so that getters running on other threads see
    // empty slots instead of half-freed decoders.
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_StealDecoder(player, i, &decoders[i], &dec_threads[i]);
    }

    // Signal all pipeline threads to quit, and unblock any buffer waits so the joins below cannot hang.
    Kit_StopDemuxerThread(player->demux_thread);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_StopDecoderThread(dec_threads[i]);
    }
    Kit_AbortDemuxer(player->demuxer);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_AbortDecoder(decoders[i]);
    }

    // Join the threads and free everything.
    Kit_CloseDemuxerThread(&player->demux_thread);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_CloseDecoderThread(&dec_threads[i]);
    }
    Kit_CloseDemuxer(&player->demuxer);
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_CloseDecoder(&decoders[i]);
    }
    Kit_CloseTimer(&player->sync_timer);
    SDL_UnlockMutex(player->control_lock);

    SDL_DestroyMutex(player->control_lock);
    for(int i = 0; i < KIT_INDEX_COUNT; i++)
        SDL_DestroyMutex(player->decoder_ctrl_locks[i]);
    memset(player, 0, sizeof(Kit_Player));
    free(player);
}

void Kit_SetPlayerScreenSize(Kit_Player *player, int w, int h) {
    assert(player != NULL);
    Kit_LockDecoderCtrl(player, KIT_SUBTITLE_INDEX);
    const Kit_Decoder *dec = player->decoders[KIT_SUBTITLE_INDEX];
    if(dec != NULL) {
        player->screen_w = w;
        player->screen_h = h;
        Kit_SetSubtitleDecoderSize(dec, w, h);
    }
    Kit_UnlockDecoderCtrl(player, KIT_SUBTITLE_INDEX);
}

static int Kit_GetPlayerStreamIndex(const Kit_Player *player, Kit_BufferIndex index) {
    if(player == NULL)
        return -1;
    Kit_LockDecoderCtrl(player, index);
    const int stream_index = Kit_GetDecoderStreamIndex(player->decoders[index]);
    Kit_UnlockDecoderCtrl(player, index);
    return stream_index;
}

int Kit_GetPlayerVideoStream(const Kit_Player *player) {
    return Kit_GetPlayerStreamIndex(player, KIT_VIDEO_INDEX);
}

int Kit_GetPlayerAudioStream(const Kit_Player *player) {
    return Kit_GetPlayerStreamIndex(player, KIT_AUDIO_INDEX);
}

int Kit_GetPlayerSubtitleStream(const Kit_Player *player) {
    return Kit_GetPlayerStreamIndex(player, KIT_SUBTITLE_INDEX);
}

int Kit_GetPlayerVideoSDLTexture(const Kit_Player *player, SDL_Texture *texture, SDL_Rect *area) {
    assert(player != NULL);
    int ret = 0;
    Kit_LockDecoderCtrl(player, KIT_VIDEO_INDEX);
    Kit_Decoder *decoder = player->decoders[KIT_VIDEO_INDEX];
    const Kit_PlayerState state = Kit_GetState(player);
    if(decoder != NULL && state != KIT_PAUSED && state != KIT_STOPPED)
        ret = Kit_GetVideoDecoderSDLTexture(decoder, texture, area);
    Kit_UnlockDecoderCtrl(player, KIT_VIDEO_INDEX);
    return ret;
}

int Kit_LockPlayerVideoRawFrame(const Kit_Player *player, unsigned char ***data, int **line_size, SDL_Rect *area) {
    assert(player != NULL);
    Kit_LockDecoderCtrl(player, KIT_VIDEO_INDEX);
    Kit_Decoder *decoder = player->decoders[KIT_VIDEO_INDEX];
    const Kit_PlayerState state = Kit_GetState(player);
    int ret = 1;
    if(decoder != NULL && state != KIT_PAUSED && state != KIT_STOPPED)
        ret = Kit_LockVideoDecoderRaw(decoder, data, line_size, area);
    if(ret != 0)
        Kit_UnlockDecoderCtrl(player, KIT_VIDEO_INDEX);
    return ret;
}

void Kit_UnlockPlayerVideoRawFrame(const Kit_Player *player) {
    assert(player != NULL);
    // The decoder ctrl lock is still held here from a successful Kit_LockPlayerVideoRawFrame().
    Kit_Decoder *decoder = player->decoders[KIT_VIDEO_INDEX];
    if(decoder != NULL)
        Kit_UnlockVideoDecoderRaw(decoder);
    Kit_UnlockDecoderCtrl(player, KIT_VIDEO_INDEX);
}

int Kit_GetPlayerAudioData(
    const Kit_Player *player, size_t backend_buffer_size, unsigned char *buffer, size_t length
) {
    assert(player != NULL);
    assert(buffer != NULL);
    if(length == 0)
        return 0;
    int ret = 0;
    Kit_LockDecoderCtrl(player, KIT_AUDIO_INDEX);
    Kit_Decoder *decoder = player->decoders[KIT_AUDIO_INDEX];
    const Kit_PlayerState state = Kit_GetState(player);
    if(decoder != NULL && state != KIT_PAUSED && state != KIT_STOPPED)
        ret = Kit_GetAudioDecoderData(decoder, backend_buffer_size, buffer, length);
    Kit_UnlockDecoderCtrl(player, KIT_AUDIO_INDEX);
    return ret;
}

int Kit_GetPlayerSubtitleSDLTexture(
    const Kit_Player *player, SDL_Texture *texture, SDL_Rect *sources, SDL_Rect *targets, int limit
) {
    assert(player != NULL);
    assert(texture != NULL);
    assert(sources != NULL);
    assert(targets != NULL);
    assert(limit >= 0);

    int ret = 0;
    Kit_LockDecoderCtrl(player, KIT_SUBTITLE_INDEX);
    const Kit_Decoder *sub_dec = player->decoders[KIT_SUBTITLE_INDEX];
    const Kit_PlayerState state = Kit_GetState(player);
    if(sub_dec == NULL || state == KIT_STOPPED) {
        // If stopped, do nothing.
        // LOG("STOPPED, no subtitle data");
    } else if(state == KIT_PAUSED) {
        // If paused, just return the current items.
        ret = Kit_GetSubtitleDecoderSDLTextureInfo(sub_dec, sources, targets, limit);
    } else {
        Kit_GetSubtitleDecoderSDLTexture(sub_dec, texture, Kit_GetTimerElapsed(player->sync_timer));
        ret = Kit_GetSubtitleDecoderSDLTextureInfo(sub_dec, sources, targets, limit);
    }
    Kit_UnlockDecoderCtrl(player, KIT_SUBTITLE_INDEX);
    return ret;
}

int Kit_GetPlayerSubtitleRawFrames(
    const Kit_Player *player, unsigned char ***items, SDL_Rect **sources, SDL_Rect **targets
) {
    assert(player != NULL);
    int ret = 0;
    Kit_LockDecoderCtrl(player, KIT_SUBTITLE_INDEX);
    const Kit_Decoder *sub_dec = player->decoders[KIT_SUBTITLE_INDEX];
    const Kit_PlayerState state = Kit_GetState(player);
    if(sub_dec != NULL && state != KIT_PAUSED && state != KIT_STOPPED) {
        ret =
            Kit_GetSubtitleDecoderRawFrames(sub_dec, items, sources, targets, Kit_GetTimerElapsed(player->sync_timer));
    }
    Kit_UnlockDecoderCtrl(player, KIT_SUBTITLE_INDEX);
    return ret;
}

void Kit_GetPlayerInfo(const Kit_Player *player, Kit_PlayerInfo *info) {
    assert(player != NULL);
    assert(info != NULL);

    // Lock video decoder for reading, read info, unlock.
    Kit_LockDecoderCtrl(player, KIT_VIDEO_INDEX);
    const Kit_Decoder *video_decoder = player->decoders[KIT_VIDEO_INDEX];
    Kit_GetDecoderCodecInfo(video_decoder, &info->video_codec);
    Kit_GetVideoDecoderOutputFormat(video_decoder, &info->video_format);
    Kit_UnlockDecoderCtrl(player, KIT_VIDEO_INDEX);

    // Lock audio decoder for reading, read info, unlock.
    Kit_LockDecoderCtrl(player, KIT_AUDIO_INDEX);
    const Kit_Decoder *audio_decoder = player->decoders[KIT_AUDIO_INDEX];
    Kit_GetDecoderCodecInfo(audio_decoder, &info->audio_codec);
    Kit_GetAudioDecoderOutputFormat(audio_decoder, &info->audio_format);
    Kit_UnlockDecoderCtrl(player, KIT_AUDIO_INDEX);

    // Lock subtitle decoder for reading, read info, unlock.
    Kit_LockDecoderCtrl(player, KIT_SUBTITLE_INDEX);
    const Kit_Decoder *subtitle_decoder = player->decoders[KIT_SUBTITLE_INDEX];
    Kit_GetDecoderCodecInfo(subtitle_decoder, &info->subtitle_codec);
    Kit_GetSubtitleDecoderOutputFormat(subtitle_decoder, &info->subtitle_format);
    Kit_UnlockDecoderCtrl(player, KIT_SUBTITLE_INDEX);
}

SDL_Texture *Kit_CreatePlayerVideoSDLTexture(const Kit_Player *player, SDL_Renderer *renderer, int w, int h) {
    if(player == NULL || renderer == NULL) {
        Kit_SetError("Player and renderer must not be NULL");
        return NULL;
    }

    Kit_VideoOutputFormat format;
    Kit_LockDecoderCtrl(player, KIT_VIDEO_INDEX);
    const bool has_video = player->decoders[KIT_VIDEO_INDEX] != NULL;
    Kit_GetVideoDecoderOutputFormat(player->decoders[KIT_VIDEO_INDEX], &format);
    Kit_UnlockDecoderCtrl(player, KIT_VIDEO_INDEX);

    if(!has_video) {
        Kit_SetError("Player has no video stream");
        return NULL;
    }
    SDL_Texture *texture = SDL_CreateTexture(
        renderer, format.format, SDL_TEXTUREACCESS_STATIC, w > 0 ? w : format.width, h > 0 ? h : format.height
    );
    if(texture == NULL) {
        Kit_SetError("Unable to create video texture: %s", SDL_GetError());
        return NULL;
    }
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
#endif
    return texture;
}

SDL_Texture *Kit_CreatePlayerSubtitleSDLTexture(const Kit_Player *player, SDL_Renderer *renderer, int w, int h) {
    SDL_RendererInfo renderer_info;
    if(player == NULL || renderer == NULL) {
        Kit_SetError("Player and renderer must not be NULL");
        return NULL;
    }

    Kit_SubtitleOutputFormat format;
    Kit_LockDecoderCtrl(player, KIT_SUBTITLE_INDEX);
    const bool has_subtitles = player->decoders[KIT_SUBTITLE_INDEX] != NULL;
    Kit_GetSubtitleDecoderOutputFormat(player->decoders[KIT_SUBTITLE_INDEX], &format);
    Kit_UnlockDecoderCtrl(player, KIT_SUBTITLE_INDEX);

    if(!has_subtitles) {
        Kit_SetError("Player has no subtitle stream");
        return NULL;
    }
    if(w <= 0 || h <= 0) {
        int max_w = 4096;
        int max_h = 4096;
        if(SDL_GetRendererInfo(renderer, &renderer_info) == 0) {
            if(renderer_info.max_texture_width > 0)
                max_w = Kit_min(max_w, renderer_info.max_texture_width);
            if(renderer_info.max_texture_height > 0)
                max_h = Kit_min(max_h, renderer_info.max_texture_height);
        }
        if(w <= 0)
            w = max_w;
        if(h <= 0)
            h = max_h;
    }
    SDL_Texture *texture = SDL_CreateTexture(renderer, format.format, SDL_TEXTUREACCESS_STATIC, w, h);
    if(texture == NULL) {
        Kit_SetError("Unable to create subtitle texture: %s", SDL_GetError());
        return NULL;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
#endif
    return texture;
}

int Kit_HasBufferFillRate(
    const Kit_Player *player, int audio_input, int audio_output, int video_input, int video_output
) {
    if(video_output != -1 || video_input != -1) {
        unsigned int fl, fc, pl, pc;
        Kit_GetPlayerVideoBufferState(player, &fl, &fc, &pl, &pc);
        if(video_output > -1 && fc > 0) {
            const float value = fl / (float)fc;
            const float limit = Kit_clamp(video_output, 0, 100) / 100.0f;
            if(value < limit) {
                return 0;
            }
        }
        if(video_input > -1 && pc > 0) {
            const float value = pl / (float)pc;
            const float limit = Kit_clamp(video_input, 0, 100) / 100.0f;
            if(value < limit) {
                return 0;
            }
        }
    }
    if(audio_output != -1 || audio_input != -1) {
        unsigned int sl, sc, pl, pc;
        Kit_GetPlayerAudioBufferState(player, &sl, &sc, &pl, &pc);
        if(audio_output > -1 && sc > 0) {
            const float value = sl / (float)sc;
            const float limit = Kit_clamp(audio_output, 0, 100) / 100.0f;
            if(value < limit) {
                return 0;
            }
        }
        if(audio_input > -1 && pc > 0) {
            const float value = pl / (float)pc;
            const float limit = Kit_clamp(audio_input, 0, 100) / 100.0f;
            if(value < limit) {
                return 0;
            }
        }
    }
    return 1;
}

int Kit_WaitBufferFillRate(
    const Kit_Player *player, int audio_input, int audio_output, int video_input, int video_output, double timeout
) {
    const double start = Kit_GetSystemTime();
    double now = start;
    while(now - start < timeout) {
        if(Kit_HasBufferFillRate(player, audio_input, audio_output, video_input, video_output)) {
            return 0;
        }
        SDL_Delay(1);
        now = Kit_GetSystemTime();
    }
    return 1;
}

void Kit_GetPlayerVideoBufferState(
    const Kit_Player *player,
    unsigned int *frames_length,
    unsigned int *frames_capacity,
    unsigned int *packets_length,
    unsigned int *packets_capacity
) {
    assert(player != NULL);
    Kit_LockDecoderCtrl(player, KIT_VIDEO_INDEX);
    Kit_GetDecoderBufferState(player->decoders[KIT_VIDEO_INDEX], frames_length, frames_capacity);
    Kit_UnlockDecoderCtrl(player, KIT_VIDEO_INDEX);
    Kit_GetDemuxerBufferState(player->demuxer, KIT_VIDEO_INDEX, packets_length, packets_capacity);
}

void Kit_GetPlayerAudioBufferState(
    const Kit_Player *player,
    unsigned int *samples_length,
    unsigned int *samples_capacity,
    unsigned int *packets_length,
    unsigned int *packets_capacity
) {
    assert(player != NULL);
    Kit_LockDecoderCtrl(player, KIT_AUDIO_INDEX);
    Kit_GetDecoderBufferState(player->decoders[KIT_AUDIO_INDEX], samples_length, samples_capacity);
    Kit_UnlockDecoderCtrl(player, KIT_AUDIO_INDEX);
    Kit_GetDemuxerBufferState(player->demuxer, KIT_AUDIO_INDEX, packets_length, packets_capacity);
}

void Kit_GetPlayerSubtitleBufferState(
    const Kit_Player *player,
    unsigned int *items_length,
    unsigned int *items_capacity,
    unsigned int *packets_length,
    unsigned int *packets_capacity
) {
    assert(player != NULL);
    Kit_LockDecoderCtrl(player, KIT_SUBTITLE_INDEX);
    Kit_GetDecoderBufferState(player->decoders[KIT_SUBTITLE_INDEX], items_length, items_capacity);
    Kit_UnlockDecoderCtrl(player, KIT_SUBTITLE_INDEX);
    Kit_GetDemuxerBufferState(player->demuxer, KIT_SUBTITLE_INDEX, packets_length, packets_capacity);
}

Kit_PlayerState Kit_GetPlayerState(Kit_Player *player) {
    assert(player != NULL);
    // Not just a read -- state verification may stop and join finished pipeline threads.
    SDL_LockMutex(player->control_lock);
    Kit_VerifyState(player);
    SDL_UnlockMutex(player->control_lock);
    return Kit_GetState(player);
}

void Kit_PlayerPlay(Kit_Player *player) {
    assert(player != NULL);
    SDL_LockMutex(player->control_lock);
    switch(Kit_GetState(player)) {
        case KIT_PLAYING:
        case KIT_CLOSED:
            break;
        case KIT_PAUSED:
            Kit_ResumeTimer(player->sync_timer);
            Kit_SetState(player, KIT_PLAYING);
            break;
        case KIT_STOPPED:
            Kit_StartThreads(player);
            Kit_ResetTimerBase(player->sync_timer);
            Kit_SetState(player, KIT_PLAYING);
            break;
    }
    SDL_UnlockMutex(player->control_lock);
}

void Kit_PlayerStop(Kit_Player *player) {
    assert(player != NULL);
    SDL_LockMutex(player->control_lock);
    switch(Kit_GetState(player)) {
        case KIT_STOPPED:
        case KIT_CLOSED:
            break;
        case KIT_PLAYING:
        case KIT_PAUSED:
            Kit_SetState(player, KIT_STOPPED);
            Kit_StopThreads(player);
            Kit_AbortAllBuffers(player);
            Kit_WaitThreads(player);
            Kit_FlushAllBuffers(player);
            break;
    }
    SDL_UnlockMutex(player->control_lock);
}

void Kit_PlayerPause(Kit_Player *player) {
    assert(player != NULL);
    SDL_LockMutex(player->control_lock);
    if(Kit_GetState(player) == KIT_PLAYING) {
        Kit_PauseTimer(player->sync_timer);
        Kit_SetState(player, KIT_PAUSED);
    }
    SDL_UnlockMutex(player->control_lock);
}

int Kit_PlayerSeek(Kit_Player *player, double seek_set) {
    assert(player != NULL);
    SDL_LockMutex(player->control_lock);
    const Kit_PlayerState state = Kit_GetState(player);
    if(state == KIT_STOPPED || state == KIT_CLOSED) {
        SDL_UnlockMutex(player->control_lock);
        Kit_SetError("Player is closed");
        return 1;
    }
    const double duration = Kit_GetPlayerDuration(player);
    if(seek_set <= 0)
        seek_set = 0;
    if(seek_set >= duration)
        seek_set = duration;

    // Halt the whole pipeline first to avoid race conditions
    Kit_StopThreads(player);
    Kit_AbortAllBuffers(player);
    Kit_WaitThreads(player);
    Kit_FlushAllBuffers(player);

    // Request the seek only now that nothing is running. This ensures the seek packet is read
    // immediately on thread start.
    Kit_SeekDemuxerThread(player->demux_thread, seek_set * AV_TIME_BASE);
    Kit_StartThreads(player);
    SDL_UnlockMutex(player->control_lock);

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
    int ret = 1;
    Kit_LockDecoderCtrl(player, KIT_VIDEO_INDEX);
    const Kit_Decoder *decoder = player->decoders[KIT_VIDEO_INDEX];
    if(!decoder) {
        Kit_SetError("Unable to find aspect ratio; no video stream selected");
        goto exit;
    }

    // First off, try to get the aspect ratio of the currently showing frame.
    // This may change frame-to-frame.
    if(IS_RATIONAL_DEFINED(decoder->aspect_ratio)) {
        *num = decoder->aspect_ratio.num;
        *den = decoder->aspect_ratio.den;
        ret = 0;
        goto exit;
    }

    // Then, try to find aspect ratio from the decoder itself
    const AVCodecContext *codec_ctx = decoder->codec_ctx;
    if(IS_RATIONAL_DEFINED(codec_ctx->sample_aspect_ratio)) {
        *num = codec_ctx->sample_aspect_ratio.num;
        *den = codec_ctx->sample_aspect_ratio.den;
        ret = 0;
        goto exit;
    }

    // Then, try the stream (demuxer) data
    const AVStream *stream = decoder->stream;
    if(IS_RATIONAL_DEFINED(stream->sample_aspect_ratio)) {
        *num = stream->sample_aspect_ratio.num;
        *den = stream->sample_aspect_ratio.den;
        ret = 0;
        goto exit;
    }

    // No data found anywhere, give up.
    Kit_SetError("Unable to find aspect ratio; no data from demuxer or codec");

exit:
    Kit_UnlockDecoderCtrl(player, KIT_VIDEO_INDEX);
    return ret;
}

static void Kit_IsStreamPrimary(const Kit_Player *player, bool *video_primary, bool *audio_primary) {
    const Kit_Decoder *video_decoder = player->decoders[KIT_VIDEO_INDEX];
    const Kit_Decoder *audio_decoder = player->decoders[KIT_AUDIO_INDEX];
    *video_primary = video_decoder && video_decoder->stream->index > -1;
    *audio_primary = audio_decoder && !*video_primary && audio_decoder->stream->index > -1;
}

int Kit_ClosePlayerStream(Kit_Player *player, const Kit_StreamType type) {
    assert(player != NULL);

    Kit_BufferIndex buffer_index;
    switch(type) {
        case KIT_STREAMTYPE_AUDIO:
            buffer_index = KIT_AUDIO_INDEX;
            break;
        case KIT_STREAMTYPE_VIDEO:
            buffer_index = KIT_VIDEO_INDEX;
            break;
        case KIT_STREAMTYPE_SUBTITLE:
            buffer_index = KIT_SUBTITLE_INDEX;
            break;
        default:
            Kit_SetError("Unknown stream type");
            return 1;
    }

    // Detach the old decoder first, so that getters on other threads see an empty slot,
    // then stop it and clear the output buffers.
    Kit_Decoder *old_decoder;
    Kit_DecoderThread *old_thread;
    SDL_LockMutex(player->control_lock);
    Kit_StealDecoder(player, buffer_index, &old_decoder, &old_thread);
    Kit_HaltDecoder(old_decoder, old_thread);

    // Clear the demuxer packets
    Kit_SetDemuxerStreamIndex(player->demuxer, buffer_index, -1);
    SDL_UnlockMutex(player->control_lock);
    return 0;
}

int Kit_SetPlayerStream(Kit_Player *player, const Kit_StreamType type, int index) {
    assert(player != NULL);
    Kit_Decoder *new_decoder = NULL;
    Kit_DecoderThread *new_thread = NULL;
    Kit_BufferIndex buffer_index;
    bool video_primary, audio_primary;

    // If index is -1, it means we are closing the stream.
    if(index < 0) {
        return Kit_ClosePlayerStream(player, type);
    }

    // Figure out which stream is currently the primary one. This stream is allowed to modify the sync clock.
    // Note that decoder creation happens under the control lock only -- it can be slow, and getters
    // on other threads must not block on it. The decoder control lock is taken just for the pointer swap below.
    SDL_LockMutex(player->control_lock);
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
                   &player->audio_req,
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
                   &player->video_req,
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
            SDL_UnlockMutex(player->control_lock);
            Kit_SetError("Unknown stream type");
            return 1;
    }

    // If we have a good new decoder, detach the old decoder from the player and stop it.
    Kit_Decoder *old_decoder;
    Kit_DecoderThread *old_thread;
    Kit_StealDecoder(player, buffer_index, &old_decoder, &old_thread);
    Kit_HaltDecoder(old_decoder, old_thread);

    // Switch demuxer to track the new stream index. This will also clear the packet buffer, so that the decoder
    // will no longer get packets from the old stream.
    Kit_SetDemuxerStreamIndex(player->demuxer, buffer_index, index);

    // EOF packet may have been lost in the flush; resend it if needed.
    if(!Kit_IsDemuxerThreadAlive(player->demux_thread))
        Kit_SendDemuxerEOFPacket(player->demuxer, buffer_index);

    // Set the new decoder and thread, and spin up the thread if we were already playing.
    Kit_LockDecoderCtrl(player, buffer_index);
    player->decoders[buffer_index] = new_decoder;
    player->dec_threads[buffer_index] = new_thread;
    Kit_UnlockDecoderCtrl(player, buffer_index);
    const Kit_PlayerState state = Kit_GetState(player);
    if(state == KIT_PLAYING || state == KIT_PAUSED)
        Kit_StartThreadFor(player, buffer_index);
    SDL_UnlockMutex(player->control_lock);

    // Et voila!
    return 0;

error_1:
    SDL_UnlockMutex(player->control_lock);
    Kit_SetError("Failed to initialize decoder");
    Kit_CloseDecoder(&new_decoder);
    Kit_CloseDecoderThread(&new_thread);
    return 1;
}

int Kit_GetPlayerStream(const Kit_Player *player, const Kit_StreamType type) {
    Kit_BufferIndex buffer_index;
    switch(type) {
        case KIT_STREAMTYPE_AUDIO:
            buffer_index = KIT_AUDIO_INDEX;
            break;
        case KIT_STREAMTYPE_VIDEO:
            buffer_index = KIT_VIDEO_INDEX;
            break;
        case KIT_STREAMTYPE_SUBTITLE:
            buffer_index = KIT_SUBTITLE_INDEX;
            break;
        default:
            return -1;
    }
    return Kit_GetPlayerStreamIndex(player, buffer_index);
}