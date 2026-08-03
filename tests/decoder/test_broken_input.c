/**
 * Broken-input robustness tests: malformed/truncated/corrupted fixtures against
 * Kit_CreateSourceFromUrl() and full playback. Primarily ASan/hang-regression
 * tests, not behavioral-contract tests: only "no crash", "no hang" (wall-clock
 * bounded, 120s CTest TIMEOUT backstop), "failed opens leave Kit_GetError()",
 * and "clean teardown" are asserted; outcomes are deliberately loose since
 * FFmpeg's actual behavior varies by version.
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

#include "kit_assert.h"
#include "kit_lifecycle.h"
#include "kit_playback.h"

#include <SDL.h>

#include "kitchensink2/kitchensink.h"

#define GARBAGE_FILE KIT_TEST_DATA_DIR "/garbage.mp4"
#define EMPTY_FILE KIT_TEST_DATA_DIR "/empty.mp4"
#define TRUNCATED_FILE KIT_TEST_DATA_DIR "/truncated.mp4"
#define CORRUPT_MIDDLE_FILE KIT_TEST_DATA_DIR "/corrupt_middle.mp4"
#define BROKEN_SRT_FILE KIT_TEST_DATA_DIR "/broken.srt"

#define SCREEN_W 160
#define SCREEN_H 120

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or let a failed test's live player threads
 * cascade into (and leak across) the remaining tests in the group. */
typedef struct {
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

/** @brief Per-test teardown: releases whatever the TestState still holds (player first, since
 * Kit_ClosePlayer joins its threads), then the state itself. Tests NULL each member right after
 * their own close, so only what an assert-longjmp left behind is released here. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
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

// -- test_garbage_file_rejected ----------------------------------------

/**
 * @brief 4096 bytes of random data with a ".mp4" extension fails to open cleanly.
 */
static void test_garbage_file_rejected(void **state) {
    (void)state;

    // Act / Assert
    assert_source_open_fails_cleanly(GARBAGE_FILE);
}

// -- test_empty_file_rejected ------------------------------------------

/**
 * @brief A genuinely zero-byte file fails to open cleanly (no format can be probed).
 */
static void test_empty_file_rejected(void **state) {
    (void)state;

    // Act / Assert
    assert_source_open_fails_cleanly(EMPTY_FILE);
}

// -- test_truncated_file -----------------------------------------------

/**
 * @brief A file truncated to 25% of its bytes (moov atom unreachable) fails to open cleanly, or plays/drains without
 * hanging.
 */
static void test_truncated_file(void **state) {
    TestState *ts = *state;

    // Act
    Kit_ClearError();
    ts->src = Kit_CreateSourceFromUrl(TRUNCATED_FILE);

    // Assert: whichever way it goes, it must be clean.
    if(ts->src == NULL) {
        assert_non_null(Kit_GetError());
        return;
    }

    // Open succeeded: play/drain to the end of whatever data exists, bounded.
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    ts->player = Kit_CreatePlayer(ts->src, video_index, audio_index, -1, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    if(ts->player == NULL) {
        // Player creation also failed cleanly -- acceptable for a truncated file.
        assert_non_null(Kit_GetError());
        Kit_CloseSource(ts->src);
        ts->src = NULL;
        return;
    }

    if(video_index >= 0) {
        create_headless_renderer(SCREEN_W, SCREEN_H, &ts->screen, &ts->renderer);
        ts->texture = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
        assert_non_null(ts->texture);
    }
    unsigned char audio_buffer[8192];

    Kit_PlayerPlay(ts->player);
    assert_true(
        pump_av_until_idle(ts->player, ts->texture, audio_index >= 0 ? audio_buffer : NULL, sizeof(audio_buffer))
    );

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    if(ts->texture != NULL)
        SDL_DestroyTexture(ts->texture);
    ts->texture = NULL;
    if(ts->renderer != NULL)
        SDL_DestroyRenderer(ts->renderer);
    ts->renderer = NULL;
    if(ts->screen != NULL)
        SDL_FreeSurface(ts->screen);
    ts->screen = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_corrupt_middle_survives_playback -----------------------------

/**
 * @brief A file with random bytes corrupting compressed sample data (moov intact) opens fine and survives playback.
 * Decode errors are tolerated; only requirement is no hang or crash.
 */
static void test_corrupt_middle_survives_playback(void **state) {
    TestState *ts = *state;

    // Arrange
    ts->src = Kit_CreateSourceFromUrl(CORRUPT_MIDDLE_FILE);
    assert_non_null(ts->src); // header/moov is untouched
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    ts->player = Kit_CreatePlayer(ts->src, video_index, audio_index, -1, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    assert_non_null(ts->player);

    if(video_index >= 0) {
        create_headless_renderer(SCREEN_W, SCREEN_H, &ts->screen, &ts->renderer);
        ts->texture = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
        assert_non_null(ts->texture);
    }
    unsigned char audio_buffer[8192];

    // Act: play through the whole file, including the corrupted region.
    // No assertion on how it settles -- the regression this guards is
    // pump_av_until_idle() never returning at all.
    Kit_PlayerPlay(ts->player);
    pump_av_until_idle(ts->player, ts->texture, audio_index >= 0 ? audio_buffer : NULL, sizeof(audio_buffer));

    // Assert: the player is still in a well-behaved state -- state query and
    // stop/close must not crash even after decode errors mid-stream.
    Kit_PlayerState final_state = Kit_GetPlayerState(ts->player);
    assert_true(
        final_state == KIT_PLAYING || final_state == KIT_STOPPED || final_state == KIT_PAUSED ||
        final_state == KIT_CLOSED
    );

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    if(ts->texture != NULL)
        SDL_DestroyTexture(ts->texture);
    ts->texture = NULL;
    if(ts->renderer != NULL)
        SDL_DestroyRenderer(ts->renderer);
    ts->renderer = NULL;
    if(ts->screen != NULL)
        SDL_FreeSurface(ts->screen);
    ts->screen = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_broken_srt ---------------------------------------------------

/**
 * @brief broken.srt (unparseable timestamp, end-before-start cue, empty-text cue) opened directly fails cleanly, or is
 * playable.
 */
static void test_broken_srt(void **state) {
    TestState *ts = *state;

    // Act
    Kit_ClearError();
    ts->src = Kit_CreateSourceFromUrl(BROKEN_SRT_FILE);

    // Assert: primary observed outcome -- open fails cleanly.
    if(ts->src == NULL) {
        assert_non_null(Kit_GetError());
        return;
    }

    // Fallback: some other FFmpeg build parsed it. A subtitle-stream player
    // must still not crash, whether or not a subtitle stream is even found.
    const int sub_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE);
    ts->player = Kit_CreatePlayer(ts->src, -1, -1, sub_index, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    if(ts->player != NULL) {
        Kit_PlayerPlay(ts->player);
        unsigned char audio_buffer[8192];
        pump_av_until_idle(ts->player, NULL, audio_buffer, sizeof(audio_buffer));
        Kit_PlayerStop(ts->player);
        Kit_ClosePlayer(ts->player);
        ts->player = NULL;
    } else {
        assert_non_null(Kit_GetError());
    }
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_garbage_file_rejected, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_empty_file_rejected, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_truncated_file, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_corrupt_middle_survives_playback, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_broken_srt, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, kit_lifecycle_setup_video_ass, kit_lifecycle_teardown_video);
}
