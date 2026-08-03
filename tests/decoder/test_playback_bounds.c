/**
 * EOF and boundary-media playback tests over video_audio.mp4 plus the
 * single_frame.mp4 and short_audio.m4a boundary fixtures. The lazy
 * end-of-media transition to KIT_STOPPED settles at the start of every
 * state-dependent call, seeking a stopped player restarts playback, and
 * both data getters report distinct "no data" codes post-stop.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <SDL.h>
#include <SDL_timer.h>

#include "kitchensink2/kitchensink.h"

#include "kit_lifecycle.h"
#include "kit_playback.h"

#define SINGLE_FRAME_FILE KIT_TEST_DATA_DIR "/single_frame.mp4"
#define SHORT_AUDIO_FILE KIT_TEST_DATA_DIR "/short_audio.m4a"

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or let a failed test's live player threads
 * cascade into (and leak across) the remaining tests in the group. Embeds a PlayerFixture by
 * value for the tests that use one; the other members serve the tests that build their player
 * directly. */
typedef struct {
    PlayerFixture fx;
    Kit_Source *src;
    Kit_Player *player;
    SDL_Surface *screen;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: releases whatever the TestState still holds (the embedded fixture
 * first -- close_fixture() is safe on a zeroed or partially built one -- then the direct members,
 * player before its render target since Kit_ClosePlayer joins its threads), then the state itself.
 * Tests NULL each member right after their own close, so only what an assert-longjmp left behind
 * is released here. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    close_fixture(&ts->fx);
    Kit_ClosePlayer(ts->player);
    if(ts->texture != NULL)
        SDL_DestroyTexture(ts->texture);
    if(ts->renderer != NULL)
        SDL_DestroyRenderer(ts->renderer);
    if(ts->screen != NULL)
        SDL_FreeSurface(ts->screen);
    Kit_CloseSource(ts->src);
    free(ts);
    *state = NULL;
    return 0;
}

// -- test_playback_reaches_eof -----------------------------------------

/**
 * @brief Canonical hang-regression test: video+audio playback drains cleanly to EOF and Kit_GetPlayerState() then
 * reports STOPPED. Never polls Kit_GetPlayerState() before EOF is declared, since that would flip state as a side
 * effect.
 */
static void test_playback_reaches_eof(void **state) {
    TestState *ts = *state;

    // Arrange
    PlayerFixture *fx = &ts->fx;
    create_fixture(fx);
    const double duration = Kit_GetPlayerDuration(fx->player);
    unsigned char audio_buffer[8192];

    // Act: drain both getters until EOF (10 consecutive empty iterations).
    // This can trigger slightly before position reaches `duration`, so a
    // second bounded wait below lets it catch up.
    Kit_PlayerPlay(fx->player);
    assert_true(drain_to_idle(fx, 10));

    // Position must catch up to the clamped duration shortly afterwards.
    const Uint32 pos_wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - pos_wait_start < WAIT_BOUND_MS && Kit_GetPlayerPosition(fx->player) < duration)
        SDL_Delay(5);
    assert_true(Kit_GetPlayerPosition(fx->player) == duration);

    // Getters must keep returning "no data" cleanly for a while longer,
    // without error or crash, while the raw state is still KIT_PLAYING.
    for(int i = 0; i < 20; i++) {
        assert_int_equal(pump_audio_once(fx->player, audio_buffer, sizeof(audio_buffer)), 0);
        assert_false(pump_video_once(fx->player, fx->texture));
    }

    // Only now poll Kit_GetPlayerState(): expected to lazily flip to KIT_STOPPED.
    assert_int_equal(Kit_GetPlayerState(fx->player), KIT_STOPPED);

    close_fixture(fx);
}

// -- test_seek_after_eof -----------------------------------------------

/**
 * @brief Seeking after end of media is deterministic: the player settles to KIT_STOPPED, the seek
 * succeeds, playback restarts (KIT_PLAYING) and data flows again.
 */
static void test_seek_after_eof(void **state) {
    TestState *ts = *state;

    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_AUDIO_FILE);
    assert_non_null(ts->src);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    assert_true(audio_index >= 0);
    ts->player = Kit_CreatePlayer(ts->src, -1, audio_index, -1, NULL, NULL, 0, 0, NULL);
    assert_non_null(ts->player);

    unsigned char buffer[8192];
    Kit_PlayerPlay(ts->player);

    // Act: drain to EOF (10 consecutive empty reads), bounded.
    assert_true(pump_av_until_idle(ts->player, NULL, buffer, sizeof(buffer)));

    // Act: wait for the settled post-EOF state (the clock may still be a hair short of the
    // duration right after the drain), then seek back into the middle of the stream.
    const Uint32 stop_wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - stop_wait_start < WAIT_BOUND_MS && Kit_GetPlayerState(ts->player) != KIT_STOPPED)
        SDL_Delay(10);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_STOPPED);
    assert_int_equal(Kit_PlayerSeek(ts->player, 0.5), 0);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_PLAYING);

    // Assert: data flows again within a bounded wait.
    assert_true(pump_until_audio_flows(ts->player));

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_getters_after_eof --------------------------------------------

/**
 * @brief Once KIT_STOPPED is confirmed post-EOF, both getters keep returning cleanly and agree on "no data":
 * audio reports 0 bytes, video reports 1 ("no new frame") -- distinct from its "texture updated" code (0).
 */
static void test_getters_after_eof(void **state) {
    TestState *ts = *state;

    // Arrange
    PlayerFixture *fx = &ts->fx;
    create_fixture(fx);
    unsigned char buffer[8192];
    Kit_PlayerPlay(fx->player);
    const double duration = Kit_GetPlayerDuration(fx->player);

    // Act: drain to EOF, then let position catch up to `duration` (see
    // test_playback_reaches_eof for why that is a separate step) before
    // confirming the state transition -- Kit_VerifyState() only flips to
    // KIT_STOPPED once position >= duration as well as all threads dead.
    assert_true(drain_to_idle(fx, 10));

    // Poll bounded: right after the drain the pipeline threads may still be winding down
    // (sanitizer slowdown), so the STOPPED verdict can lag a moment behind position.
    const Uint32 pos_wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - pos_wait_start < WAIT_BOUND_MS &&
          (Kit_GetPlayerPosition(fx->player) < duration || Kit_GetPlayerState(fx->player) != KIT_STOPPED))
        SDL_Delay(5);
    assert_int_equal(Kit_GetPlayerState(fx->player), KIT_STOPPED);

    // Assert: getters remain well-behaved post-stop (see @brief above).
    for(int i = 0; i < 10; i++) {
        assert_int_equal(pump_audio_once(fx->player, buffer, sizeof(buffer)), 0);
        assert_int_equal(Kit_GetPlayerVideoSDLTexture(fx->player, fx->texture, NULL), 1);
    }
    assert_true(Kit_GetPlayerPosition(fx->player) == duration);

    close_fixture(fx);
}

// -- test_no_silence_past_eof ------------------------------------------

/**
 * @brief Kit_GetPlayerAudioData() with backend_buffer_size 0 (= "queue empty, pad against underrun")
 * stops synthesizing silence once the stream has fully drained at end of media: the audio decoder
 * remembers the codec-level EOF, so reads return honest zeros even while the raw player state is
 * still KIT_PLAYING. (Kit_GetPlayerState() is deliberately not polled here -- once the player
 * settles to KIT_STOPPED, the getter would return 0 for the trivial state-gate reason instead.)
 */
static void test_no_silence_past_eof(void **state) {
    TestState *ts = *state;

    // Arrange: audio-only playback, drained to the end with silence-padding disabled (SIZE_MAX).
    ts->src = Kit_CreateSourceFromUrl(VIDEO_AUDIO_FILE);
    assert_non_null(ts->src);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    assert_true(audio_index >= 0);
    ts->player = Kit_CreatePlayer(ts->src, -1, audio_index, -1, NULL, NULL, 0, 0, NULL);
    assert_non_null(ts->player);

    unsigned char buffer[8192];
    Kit_PlayerPlay(ts->player);
    const double duration = Kit_GetPlayerDuration(ts->player);
    // Drain criterion: sustained empty reads alone can fire on a mid-stream scheduling stall
    // (sanitizer slowdown), so also require the clock to have passed the media end.
    const Uint32 wait_start = SDL_GetTicks();
    int idle_streak = 0;
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS &&
          (idle_streak < 10 || Kit_GetPlayerPosition(ts->player) < duration)) {
        if(pump_audio_once(ts->player, buffer, sizeof(buffer)) == 0)
            idle_streak++;
        else
            idle_streak = 0;
        SDL_Delay(10);
    }
    assert_true(idle_streak >= 10);
    assert_true(Kit_GetPlayerPosition(ts->player) >= duration);

    // Act / Assert: with the queue reported empty (0), pre-fix behavior would synthesize silence
    // forever; post-EOF it must return 0 instead.
    for(int i = 0; i < 5; i++) {
        assert_int_equal(Kit_GetPlayerAudioData(ts->player, 0, buffer, sizeof(buffer)), 0);
        SDL_Delay(10);
    }

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_single_frame_video -------------------------------------------

/**
 * @brief A single-frame video fixture (one h264 frame, no audio) still yields that frame and reaches a clean EOF.
 */
static void test_single_frame_video(void **state) {
    TestState *ts = *state;

    // Arrange
    ts->src = Kit_CreateSourceFromUrl(SINGLE_FRAME_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    assert_true(video_index >= 0);
    ts->player = Kit_CreatePlayer(ts->src, video_index, -1, -1, NULL, NULL, 160, 120, NULL);
    assert_non_null(ts->player);

    create_headless_renderer(160, 120, &ts->screen, &ts->renderer);
    ts->texture = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
    assert_non_null(ts->texture);

    // Act: pump until the single frame is retrieved.
    Kit_PlayerPlay(ts->player);
    assert_true(wait_for_video_frame(ts->player, ts->texture));

    // Assert: continued draining reaches a clean EOF (settles via the idle
    // streak, not the wall-clock bound).
    assert_true(pump_av_until_idle(ts->player, ts->texture, NULL, 0));

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    SDL_DestroyTexture(ts->texture);
    ts->texture = NULL;
    SDL_DestroyRenderer(ts->renderer);
    ts->renderer = NULL;
    SDL_FreeSurface(ts->screen);
    ts->screen = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_short_audio_drains -------------------------------------------

/**
 * @brief A 50ms audio clip (only ever produces a partial final buffer) still delivers some data and reaches a clean
 * EOF.
 */
static void test_short_audio_drains(void **state) {
    TestState *ts = *state;

    // Arrange
    ts->src = Kit_CreateSourceFromUrl(SHORT_AUDIO_FILE);
    assert_non_null(ts->src);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    assert_true(audio_index >= 0);
    ts->player = Kit_CreatePlayer(ts->src, -1, audio_index, -1, NULL, NULL, 0, 0, NULL);
    assert_non_null(ts->player);

    // Act: drain everything the decoder ever produces, bounded by EOF (10
    // consecutive empty reads) and by the wall-clock bound.
    unsigned char buffer[8192];
    Kit_PlayerPlay(ts->player);
    size_t total_received = 0;
    int idle_streak = 0;
    const Uint32 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS && idle_streak < 10) {
        const int received = pump_audio_once(ts->player, buffer, sizeof(buffer));
        if(received > 0) {
            total_received += (size_t)received;
            idle_streak = 0;
        } else {
            idle_streak++;
        }
        SDL_Delay(10);
    }

    // Assert: EOF reached cleanly, and at least some audio was delivered.
    assert_true(idle_streak >= 10);
    assert_true(total_received > 0);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_playback_reaches_eof, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_seek_after_eof, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_getters_after_eof, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_no_silence_past_eof, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_single_frame_video, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_short_audio_drains, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, kit_lifecycle_setup_video, kit_lifecycle_teardown_video);
}
