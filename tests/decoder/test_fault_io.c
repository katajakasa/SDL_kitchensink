/**
 * Deterministic I/O-failure tests for the demuxer, via the "demux_read" and
 * "demux_seek" fault points (src/internal/kitdemuxer.c): transient read
 * errors are retried then treated as EOF, and a failed seek silently keeps
 * playing from the old position. Error surfacing to the caller is deferred
 * to the SDL3-era error API rework.
 * Built only when KIT_FAULT_INJECTION is enabled; the #else branch keeps the
 * binary buildable/runnable (empty) otherwise.
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

#include <SDL.h>
#include <SDL_timer.h>
#include <libavutil/error.h>

#include "kitchensink2/internal/kitfaultinject.h"
#include "kitchensink2/kitchensink.h"

#include "kit_lifecycle.h"
#include "kit_playback.h"

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

/** @brief Per-test teardown: disarms all fail points even when the test body failed mid-way, so an
 * armed point cannot leak into (and cascade through) the remaining tests in the group; then closes
 * whatever the fixture still holds via kit_playback_teardown() (with the points already disarmed,
 * so the close is clean) and frees it. */
static int test_teardown(void **state) {
    Kit_ResetFailPoints();
    return kit_playback_teardown(state);
}

// -- test_read_error_mid_playback --------------------------------------

/**
 * @brief A persistent read failure injected mid-playback (after data has already flowed) exhausts
 * Kit_RunDemuxer()'s retry budget and then winds playback down exactly like EOF: no hang, closable.
 * The by-design quiet EOF (no caller-visible error) is revisited with the SDL3-era error API rework
 *.
 */
static void test_read_error_mid_playback(void **state) {
    PlayerFixture *fx = *state;
    // Arrange
    create_fixture(fx);

    // Act: play and pump until data actually flows, then arm the read failure.
    Kit_PlayerPlay(fx->player);
    assert_true(wait_for_data(fx));
    Kit_SetFailPoint("demux_read", 5, -1, AVERROR(EIO));

    // Assert: playback drains to a clean stop, same as reaching real EOF.
    assert_true(drain_to_idle(fx, 10));
    assert_true(wait_for_stopped(fx->player));

    close_fixture(fx);
}

// -- test_read_error_transient -----------------------------------------

/**
 * @brief A single transient read error (armed before Play) is retried transparently by Kit_RunDemuxer()
 * (config.demuxer.read_attempts attempts, kitdemuxer.c): the retry succeeds, playback delivers data
 * normally and later winds down at the real end of the file.
 */
static void test_read_error_transient(void **state) {
    PlayerFixture *fx = *state;
    // Arrange
    create_fixture(fx);
    Kit_SetFailPoint("demux_read", 1, 1, AVERROR(EIO));

    // Act
    Kit_PlayerPlay(fx->player);

    // Assert: the injected error is absorbed by the retry -- data flows, then a clean stop at real EOF.
    assert_true(wait_for_data(fx));
    assert_true(drain_to_idle(fx, 10));
    assert_true(wait_for_stopped(fx->player));

    close_fixture(fx);
}

// -- test_seek_error_keeps_playing -------------------------------------

/**
 * @brief A failed underlying seek leaves playback intact: frames keep flowing afterwards, because the
 * sync-clock serial is only bumped together with a successful seek's SEEK rebase packet
 * (Kit_DemuxerSeek(), kitdemuxer.c). Kit_PlayerSeek() itself still reports success for the failed
 * seek -- that error-reporting gap remains open for now (seek failures are swallowed).
 *
 * The buffers are shrunk so that the demuxer cannot swallow the whole fixture up front: the wedge
 * only manifests while unread file data remains, since Kit_PlayerSeek() flushes everything buffered
 * and a fully-read file just winds down through the EOF path with or without the serial bug.
 */
static void test_seek_error_keeps_playing(void **state) {
    PlayerFixture *fx = *state;
    // Arrange: a small video packet buffer keeps most of the fixture's video unread at seek
    // time (see @brief). Audio buffers stay at their defaults -- that slack is what lets the
    // pipeline re-prime after a seek while the audio getter waits for the clock rebase.
    Kit_PlayerConfig config;
    Kit_ResetPlayerConfig(&config);
    config.video.packet_buffer_size = 4;
    create_fixture_config(fx, &config);
    Kit_PlayerPlay(fx->player);
    assert_true(wait_for_data(fx));

    // Act
    Kit_SetFailPoint("demux_seek", 1, 1, AVERROR(EIO));
    const int seek_result = Kit_PlayerSeek(fx->player, 1.0);

    // Assert: the failing seek is swallowed (return value still 0)...
    assert_int_equal(seek_result, 0);

    // ...but playback survives the failure: audio/video data still flows afterwards from the
    // pre-seek position, instead of every frame being discarded against a stale serial.
    assert_true(wait_for_data(fx));

    // Data flowing proves the demuxer thread ran, and it attempts the seek before its first
    // read -- so the injected failure has certainly fired by now.
    assert_int_equal(Kit_GetFailPointCount("demux_seek"), 1);

    close_fixture(fx);
}

// -- test_close_during_read_retry --------------------------------------

/**
 * @brief Closing a player while the demuxer sleeps in its read-retry loop returns promptly:
 * Kit_DemuxerRetryDelay() polls the abort flag in small slices instead of sleeping the whole
 * configured delay. With 100 x 10s of retry budget armed, an unabortable sleep would block
 * close for minutes; the abort must cut it to well under the single-slice bound below.
 */
static void test_close_during_read_retry(void **state) {
    PlayerFixture *fx = *state;
    // Arrange: huge retry budget, so only abortability can make close fast.
    // A small video packet buffer keeps part of the fixture unread (default
    // buffers swallow the whole 2s file, leaving no future reads to fail).
    Kit_PlayerConfig config;
    Kit_ResetPlayerConfig(&config);
    config.video.packet_buffer_size = 4;
    config.demuxer.read_attempts = 100;
    config.demuxer.read_retry_delay = 10000;
    create_fixture_config(fx, &config);
    Kit_PlayerPlay(fx->player);
    assert_true(wait_for_data(fx));

    // Act: arm a persistent read failure. The demuxer may be parked on a full
    // buffer write at this point, so keep pumping until the fail-point check
    // count moves -- Kit_GetFailPointCount() counts every consultation
    // (including the unarmed priming reads), so only a delta proves a read
    // attempt actually hit the armed point and failed.
    Kit_SetFailPoint("demux_read", 1, -1, AVERROR(EIO));
    const int baseline = Kit_GetFailPointCount("demux_read");
    unsigned char buffer[8192];
    const Uint32 fire_wait = SDL_GetTicks();
    while(SDL_GetTicks() - fire_wait < WAIT_BOUND_MS && Kit_GetFailPointCount("demux_read") == baseline) {
        pump_audio_once(fx->player, buffer, sizeof(buffer));
        pump_video_once(fx->player, fx->texture);
        SDL_Delay(10);
    }
    assert_true(Kit_GetFailPointCount("demux_read") > baseline); // the armed failure has fired
    SDL_Delay(50); // let the demuxer proceed from the failed read into the retry sleep

    const Uint32 close_start = SDL_GetTicks();
    close_fixture(fx);
    const Uint32 close_elapsed = SDL_GetTicks() - close_start;

    // Assert: close returned with time to spare under a single 10s retry
    // delay -- the regression this guards (an unabortable sleep) blocks for
    // at least one full delay, so the bound only needs to stay clearly below
    // 10s while leaving slack for sanitizer-slowed thread joins.
    assert_true(close_elapsed < 8000);
}

// -- test_eof_vs_error_code --------------------------------------------

/**
 * @brief Injecting AVERROR_EOF on the very first read stops the stream immediately -- real end-of-file is never
 * retried, unlike transient errors (test_read_error_transient). Pinned two ways: no data is ever delivered (a
 * wrongly-retried EOF would re-read successfully and play the fixture through), and the fail point is checked
 * exactly once (a retry would consult it again).
 */
static void test_eof_vs_error_code(void **state) {
    PlayerFixture *fx = *state;
    // Arrange
    unsigned char buffer[8192];
    create_fixture(fx);
    Kit_SetFailPoint("demux_read", 1, 1, AVERROR_EOF);

    // Act: pump until the lazy STOPPED flip, tracking whether any data ever arrives.
    Kit_PlayerPlay(fx->player);
    bool got_data = false;
    const Uint32 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS && Kit_GetPlayerState(fx->player) != KIT_STOPPED) {
        got_data |= pump_audio_once(fx->player, buffer, sizeof(buffer)) > 0;
        got_data |= pump_video_once(fx->player, fx->texture);
        SDL_Delay(10);
    }

    // Assert: clean wind-down with no data and no retry of the EOF'd read.
    assert_int_equal(Kit_GetPlayerState(fx->player), KIT_STOPPED);
    assert_false(got_data);
    assert_int_equal(Kit_GetFailPointCount("demux_read"), 1);

    close_fixture(fx);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_read_error_mid_playback, kit_playback_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_read_error_transient, kit_playback_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_seek_error_keeps_playing, kit_playback_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_close_during_read_retry, kit_playback_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_eof_vs_error_code, kit_playback_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}

#else

int main(void) {
    return 0;
}

#endif // KIT_FAULT_INJECTION
