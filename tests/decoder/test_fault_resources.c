/**
 * Resource-creation fault tests via the "sdl_mutex", "swr_init", and
 * "sws_init" fault points (src/internal/kitpacketbuffer.c, src/kitplayer.c,
 * src/internal/subtitle/renderers/kitsubass.c, src/internal/audio/kitaudio.c,
 * src/internal/video/kitvideo.c): sweeps Kit_CreatePlayer()'s mutex/cond
 * creation ordinals (probe-then-sweep, same pattern as test_alloc_unwind.c),
 * and pins today's actual swr/sws creation-failure behavior as a regression
 * net. Built only when KIT_FAULT_INJECTION is enabled; the #else branch
 * keeps the binary buildable/runnable (empty) otherwise.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#ifdef KIT_FAULT_INJECTION

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_timer.h>

#include <libavutil/error.h>

#include "kitchensink3/internal/kitfaultinject.h"
#include "kitchensink3/kitchensink.h"

#include "kit_faultsweep.h"
#include "kit_lifecycle.h"
#include "kit_playback.h"

/**
 * @brief Sanity bound on the number of "sdl_mutex" ordinals a single Kit_CreatePlayer()
 * call can consume; a real probe count is expected to land far below this.
 */
#define MAX_MUTEX_ORDINALS 200

/** @brief Test lifecycle setup: reset fail points and initialize the library plus SDL video. */
static int group_setup(void **state) {
    Kit_ResetFailPoints();
    return kit_lifecycle_setup_video(state);
}

/** @brief Test lifecycle teardown: shut down the library, SDL, and reset fail points. */
static int group_teardown(void **state) {
    kit_lifecycle_teardown_video(state);
    Kit_ResetFailPoints();
    return 0;
}

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or let a failed test's live player threads
 * cascade into (and leak across) the remaining tests in the group. The stream indices are the
 * probe/attempt helpers' sweep context. */
typedef struct {
    Kit_Source *src;
    Kit_Player *player;
    SDL_Surface *screen;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    int video_index;
    int audio_index;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: disarms all fail points even when the test body failed mid-way, so an
 * armed point cannot leak into (and cascade through) the remaining tests in the group; then releases
 * whatever the TestState still holds (with the points already disarmed, so the closes are clean;
 * the player first, since Kit_ClosePlayer joins its threads), then the state itself. Tests NULL
 * each member right after their own close, so only what an assert-longjmp left behind is released
 * here. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    Kit_ResetFailPoints();
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

/** @brief Creates a player from the TestState's fixture source and immediately closes it, for probing. */
static void create_and_close_player(void *ctx) {
    TestState *ts = ctx;
    Kit_Player *player = Kit_CreatePlayer(ts->src, ts->video_index, ts->audio_index, -1, NULL, NULL, 160, 120, NULL);
    if(player != NULL) {
        Kit_ClosePlayer(player);
    }
}

/** @brief Sweep attempt: player creation from the TestState's fixture source must fail cleanly. */
static void attempt_create_player(void *ctx) {
    TestState *ts = ctx;
    Kit_Player *player = Kit_CreatePlayer(ts->src, ts->video_index, ts->audio_index, -1, NULL, NULL, 160, 120, NULL);
    assert_null(player);
}

// -- test_mutex_failure_unwinds ----------------------------------------

/**
 * @brief Failing the "sdl_mutex" point at each ordinal Kit_CreatePlayer() consumes (SDL_CreateMutex/SDL_CreateCond
 * calls in Kit_CreatePlayer's own locks and every packet buffer created along the way -- the demuxer's per-stream
 * buffers plus the audio/video decoders' output buffers) yields a clean NULL + Kit_GetError(), with no leaks
 * (ASan-checked unwind).
 */
static void test_mutex_failure_unwinds(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_AUDIO_FILE);
    assert_non_null(ts->src);
    ts->video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    ts->audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    assert_true(ts->video_index >= 0);
    assert_true(ts->audio_index >= 0);

    const int ordinals = kit_probe_fail_point_count("sdl_mutex", create_and_close_player, ts);
    assert_int_in_range(ordinals, 1, MAX_MUTEX_ORDINALS);

    // Act / Assert: fail each ordinal in turn. Every sdl_mutex creation site reachable from
    // Kit_CreatePlayer sets an error message on failure today (unlike some of the "alloc"
    // ordinals in kittimer.c, see test_alloc_unwind.c).
    assert_int_equal(kit_sweep_fail_point("sdl_mutex", ordinals, attempt_create_player, ts), 0);

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_swr_failure_fails_player -------------------------------------

/**
 * @brief A single "swr_init" failure injected before Kit_CreatePlayer() reaches the audio resampler setup
 * (swr_alloc_set_opts2/swr_init in Kit_InitializeAudioDecoder, src/internal/audio/kitaudio.c) fails the whole
 * player construction cleanly: Kit_CreatePlayer() returns NULL with Kit_GetError() set, no leaks.
 */
static void test_swr_failure_fails_player(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_AUDIO_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    assert_true(video_index >= 0);
    assert_true(audio_index >= 0);
    Kit_SetFailPoint("swr_init", 1, 1, AVERROR(ENOMEM));

    // Act: capture into the state so an unexpectedly created player is released by the teardown.
    ts->player = Kit_CreatePlayer(ts->src, video_index, audio_index, -1, NULL, NULL, 160, 120, NULL);

    // Assert
    assert_null(ts->player);
    assert_non_null(Kit_GetError());

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_sws_failure --------------------------------------------------

/**
 * @brief A one-shot "sws_init" failure silently drops one lazily-created conversion (no error, no wedge), and video
 * frames still land once the fail point is consumed. Requesting RGBA32 output forces the conversion path, which the
 * fixture's native yuv420p would otherwise never exercise.
 */
static void test_sws_failure(void **state) {
    TestState *ts = *state;
    // Arrange
    Kit_VideoFormatRequest req;
    Kit_ResetVideoFormatRequest(&req);
    req.format = SDL_PIXELFORMAT_RGBA32;

    ts->src = Kit_CreateSourceFromUrl(VIDEO_AUDIO_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    assert_true(video_index >= 0);
    assert_true(audio_index >= 0);
    ts->player = Kit_CreatePlayer(ts->src, video_index, audio_index, -1, &req, NULL, 160, 120, NULL);
    assert_non_null(ts->player);

    ts->screen = NULL;
    ts->renderer = NULL;
    create_headless_renderer(160, 120, &ts->screen, &ts->renderer);
    ts->texture = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
    assert_non_null(ts->texture);

    // Act: arm a single sws_init failure before the first conversion is ever attempted, then play.
    Kit_SetFailPoint("sws_init", 1, 1, 0);
    Kit_PlayerPlay(ts->player);

    // Assert: despite the injected failure, playback is unaffected -- frames keep arriving, since
    // the fail point only blocks the one conversion attempt it lands on (see @brief).
    assert_true(wait_for_video_frame(ts->player, ts->texture));
    assert_true(wait_for_video_frame(ts->player, ts->texture));
    assert_true(Kit_GetFailPointCount("sws_init") >= 1);

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

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_mutex_failure_unwinds, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_swr_failure_fails_player, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_sws_failure, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}

#else

int main(void) {
    return 0;
}

#endif // KIT_FAULT_INJECTION
