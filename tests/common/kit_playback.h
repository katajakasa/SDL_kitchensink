/**
 * Shared playback-pumping helpers for decoder-tier tests: a video+audio
 * player fixture (create/destroy) plus non-blocking A/V pump helpers and
 * wall-clock wait/drain loops. Header-only and registry-free (no
 * fault-injection dependency), so it can be reused by non-fault tests too.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */
#ifndef KIT_PLAYBACK_H
#define KIT_PLAYBACK_H

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_timer.h>

#include "kitchensink3/kitchensink.h"

// SIZE_MAX as backend_buffer_size disables silence padding, per
// Kit_GetPlayerAudioData()'s doc comment.
#include <stdint.h>

#define VIDEO_AUDIO_FILE KIT_TEST_DATA_DIR "/video_audio.mp4"

/** @brief Hard wall-clock bound for "wait for data"/"drain until idle" loops. */
#define WAIT_BOUND_MS 15000

/**
 * @brief A video+audio player plus its headless render target, bundled so both
 * sides of the stream can always be pumped together: if video frames are
 * never drained, the decoded-frame buffer fills, backpressure stalls the
 * video decoder, and that in turn blocks the single demuxer thread from
 * ever delivering further audio packets either.
 */
typedef struct PlayerFixture {
    Kit_Source *src;
    Kit_Player *player;
    SDL_Surface *screen;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
} PlayerFixture;

/** @brief Single non-blocking audio drain attempt; returns bytes received (0 if none available). */
static inline int pump_audio_once(Kit_Player *player, unsigned char *buffer, size_t buffer_size) {
    return Kit_GetPlayerAudioData(player, SIZE_MAX, buffer, buffer_size);
}

/** @brief Single non-blocking video drain attempt; returns true if a new frame was copied into texture. */
static inline bool pump_video_once(Kit_Player *player, SDL_Texture *texture) {
    return Kit_GetPlayerVideoSDLTexture(player, texture, NULL) == 0;
}

/** @brief Creates a headless software-rendered target: an RGBA32 surface plus a software renderer over it. */
static inline void create_headless_renderer(int w, int h, SDL_Surface **screen, SDL_Renderer **renderer) {
    *screen = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    assert_non_null(*screen);
    *renderer = SDL_CreateSoftwareRenderer(*screen);
    assert_non_null(*renderer);
}

/** @brief Per-test cmocka setup: heap-allocates a zeroed PlayerFixture into *state, so an
 * assert-longjmp cannot orphan it (the paired kit_playback_teardown() always receives it). */
static inline int kit_playback_setup(void **state) {
    *state = calloc(1, sizeof(PlayerFixture));
    return *state == NULL ? -1 : 0;
}

/** @brief Creates a player from video_audio.mp4 with both streams attached, plus a headless render target.
 * The given player config (or defaults, if NULL) is used for player creation. The fixture must be the
 * heap one allocated by kit_playback_setup() -- a stack-local's memory is dead after an assert-longjmp,
 * while the teardown (and any player threads) would still reference it. */
static inline void create_fixture_config(PlayerFixture *fx, const Kit_PlayerConfig *config) {
    fx->src = NULL;
    fx->player = NULL;
    fx->screen = NULL;
    fx->renderer = NULL;
    fx->texture = NULL;
    fx->src = Kit_CreateSourceFromUrl(VIDEO_AUDIO_FILE);
    assert_non_null(fx->src);
    const int video_index = Kit_GetBestSourceStream(fx->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(fx->src, KIT_STREAMTYPE_AUDIO);
    assert_true(video_index >= 0);
    assert_true(audio_index >= 0);
    fx->player = Kit_CreatePlayer(fx->src, video_index, audio_index, -1, NULL, NULL, 160, 120, config);
    assert_non_null(fx->player);

    create_headless_renderer(160, 120, &fx->screen, &fx->renderer);
    fx->texture = Kit_CreatePlayerVideoSDLTexture(fx->player, fx->renderer, 0, 0);
    assert_non_null(fx->texture);
}

/** @brief Creates a default-configuration player fixture; see create_fixture_config(). */
static inline void create_fixture(PlayerFixture *fx) {
    create_fixture_config(fx, NULL);
}

/** @brief Tears down a PlayerFixture's player, source, and render target. Members are closed
 * only if created, so a partially built fixture (assert failure mid-create) closes cleanly. */
static inline void close_fixture(PlayerFixture *fx) {
    if(fx->player != NULL)
        Kit_ClosePlayer(fx->player);
    if(fx->texture != NULL)
        SDL_DestroyTexture(fx->texture);
    if(fx->renderer != NULL)
        SDL_DestroyRenderer(fx->renderer);
    if(fx->screen != NULL)
        SDL_DestroySurface(fx->screen);
    if(fx->src != NULL)
        Kit_CloseSource(fx->src);
    fx->player = NULL;
    fx->texture = NULL;
    fx->renderer = NULL;
    fx->screen = NULL;
    fx->src = NULL;
}

/** @brief Per-test teardown paired with kit_playback_setup(): closes whatever the fixture still
 * holds -- including everything a mid-test assert failure left open, so live player threads cannot
 * cascade into the remaining tests in the group -- and frees the fixture itself. */
static inline int kit_playback_teardown(void **state) {
    PlayerFixture *fx = *state;
    if(fx != NULL) {
        close_fixture(fx);
        free(fx);
        *state = NULL;
    }
    return 0;
}

/** @brief Pumps audio+video until at least one frame of either has been received, bounded by wall clock. */
static inline bool wait_for_data(PlayerFixture *fx) {
    unsigned char buffer[8192];
    bool received = false;
    const Uint64 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS && !received) {
        const int audio_received = pump_audio_once(fx->player, buffer, sizeof(buffer));
        const bool video_received = pump_video_once(fx->player, fx->texture);
        received = audio_received > 0 || video_received;
        if(!received)
            SDL_Delay(10);
    }
    return received;
}

/** @brief Waits (bounded) until Kit_GetPlayerState() reports KIT_STOPPED, polling it as the sole trigger for the lazy
 * EOF flip. */
static inline bool wait_for_stopped(Kit_Player *player) {
    const Uint64 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS && Kit_GetPlayerState(player) != KIT_STOPPED)
        SDL_Delay(10);
    return Kit_GetPlayerState(player) == KIT_STOPPED;
}

/** @brief Drains both audio and video until `idle_target` consecutive empty reads or the wall-clock bound; returns
 * whether idle was reached. */
static inline bool drain_to_idle(PlayerFixture *fx, int idle_target) {
    unsigned char buffer[8192];
    int idle_streak = 0;
    const Uint64 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS && idle_streak < idle_target) {
        const int audio_received = pump_audio_once(fx->player, buffer, sizeof(buffer));
        const bool video_received = pump_video_once(fx->player, fx->texture);
        if(audio_received == 0 && !video_received)
            idle_streak++;
        else
            idle_streak = 0;
        SDL_Delay(10);
    }
    return idle_streak >= idle_target;
}

/** @brief Pumps a player's video/audio getters until both go idle for 10 consecutive iterations or the wall-clock
 * bound elapses; `texture`/`audio_buffer` may be NULL to skip that half. Returns true on a clean idle settle. */
static inline bool
pump_av_until_idle(Kit_Player *player, SDL_Texture *texture, unsigned char *audio_buffer, size_t audio_buffer_size) {
    const Uint64 wait_start = SDL_GetTicks();
    int idle_streak = 0;
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS && idle_streak < 10) {
        bool got_something = false;
        if(audio_buffer != NULL && pump_audio_once(player, audio_buffer, audio_buffer_size) > 0)
            got_something = true;
        if(texture != NULL && pump_video_once(player, texture))
            got_something = true;
        if(got_something)
            idle_streak = 0;
        else
            idle_streak++;
        SDL_Delay(10);
    }
    return idle_streak >= 10;
}

/** @brief Pumps video until a frame has been received, bounded by wall clock; returns whether one arrived. */
static inline bool wait_for_video_frame(Kit_Player *player, SDL_Texture *texture) {
    const Uint64 wait_start = SDL_GetTicks();
    bool received = false;
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS && !received) {
        received = pump_video_once(player, texture);
        if(!received)
            SDL_Delay(10);
    }
    return received;
}

/** @brief Waits (bounded) until an audio read returns data; returns bytes captured (0 if none arrived). Unlike
 * pump_audio_once(), the backend buffer size is set to the read size, so silence padding stays enabled. */
static inline int wait_for_audio_data(Kit_Player *player, unsigned char *buffer, size_t buffer_size) {
    int received = 0;
    const Uint64 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS && received == 0) {
        received = Kit_GetPlayerAudioData(player, (int)buffer_size, buffer, buffer_size);
        if(received == 0)
            SDL_Delay(10);
    }
    return received;
}

/** @brief Pumps audio until a non-empty read is observed or the wall-clock bound elapses; returns true if data was
 * seen. */
static inline bool pump_until_audio_flows(Kit_Player *player) {
    unsigned char buffer[8192];
    const Uint64 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS) {
        if(pump_audio_once(player, buffer, sizeof(buffer)) > 0)
            return true;
        SDL_Delay(10);
    }
    return false;
}

/** @brief Pumps audio until reads stay empty for a sustained stretch (200 ms of consecutive silence) AND the
 * playback position has reached the stream duration; silence alone can also occur mid-stream under heavy
 * (e.g. sanitizer) load, so both are required before concluding the stream was fully drained and the demuxer
 * thread has read past the end of the file and exited. Returns true on success. */
static inline bool drain_audio_to_eof(Kit_Player *player) {
    static const int quiet_target = 20; // 20 * 10 ms = 200 ms of consecutive silence
    const double duration = Kit_GetPlayerDuration(player);
    unsigned char buffer[8192];
    int quiet = 0;
    const Uint64 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS &&
          (quiet < quiet_target || Kit_GetPlayerPosition(player) < duration)) {
        if(pump_audio_once(player, buffer, sizeof(buffer)) > 0) {
            quiet = 0;
        } else {
            quiet++;
            SDL_Delay(10);
        }
    }
    return quiet >= quiet_target && Kit_GetPlayerPosition(player) >= duration;
}

/** @brief Pumps the video texture and polls the subtitle atlas until rects appear or the wall-clock bound elapses;
 * returns the rect count (0 on timeout). Rendering is pull-driven off the player's sync clock, which only advances
 * while playback is pumped, so the video texture must be pumped alongside the subtitle getter. */
static inline int pump_until_subtitle_rects(
    Kit_Player *player, SDL_Texture *video_tex, SDL_Texture *sub_tex, SDL_Rect *sources, SDL_Rect *targets, int limit
) {
    const Uint64 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS) {
        Kit_GetPlayerVideoSDLTexture(player, video_tex, NULL);
        const int got = Kit_GetPlayerSubtitleSDLTexture(player, sub_tex, sources, targets, limit);
        if(got > 0)
            return got;
        SDL_Delay(10);
    }
    return 0;
}

#endif // KIT_PLAYBACK_H
