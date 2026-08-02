/**
 * Deterministic decode-error tests via the "decode_send" and "decode_receive"
 * fault points (src/internal/audio/kitaudio.c, src/internal/video/kitvideo.c):
 * pins today's actual error handling in the audio/video dec_input/dec_decode
 * callbacks as a regression net. Built only when KIT_FAULT_INJECTION is
 * enabled; the #else branch keeps the binary buildable/runnable otherwise.
 *
 * Both points are checked by the audio AND video decoder threads
 * concurrently (Kit_RunDecoder/Kit_AddDecoderPacket run once per stream's
 * own thread), so an armed one-shot window may be consumed by either
 * stream's decoder first. Assertions below are therefore all at the
 * playback level (does data keep flowing, does the player stay alive) and
 * never assume which stream ate the injected failure.
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

/** @brief Pumps playback (bounded) until the named fail point's lifetime check count exceeds
 * baseline + min_new_checks. Kit_GetFailPointCount() counts every consultation including
 * pre-arming ones, so only a delta from a post-arming baseline proves the armed window
 * (fired at arm_base + after_calls <= baseline + after_calls) has been passed. Pumping keeps
 * the decoder threads from parking on full output buffers while we wait. */
static bool wait_for_fail_point(PlayerFixture *fx, const char *name, const int baseline, const int min_new_checks) {
    unsigned char buffer[8192];
    const Uint32 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS && Kit_GetFailPointCount(name) <= baseline + min_new_checks) {
        pump_audio_once(fx->player, buffer, sizeof(buffer));
        pump_video_once(fx->player, fx->texture);
        SDL_Delay(10);
    }
    return Kit_GetFailPointCount(name) > baseline + min_new_checks;
}

// -- test_send_error_tolerated -----------------------------------------

/**
 * @brief A single AVERROR_INVALIDDATA injected on "decode_send" (before avcodec_send_packet) is silently treated as
 * success today: dec_input_{audio,video}_cb's switch only special-cases EOF/ENOMEM/EAGAIN, so any other code
 * (including AVERROR_INVALIDDATA) falls into the `default` "skip errors and hope for the best" branch and returns
 * KIT_DEC_INPUT_OK. The fed packet is silently dropped (never actually reaches the codec), but the decoder thread
 * treats it exactly like a normal successful send and keeps going -- so playback continues and further frames still
 * arrive after the injected failure.
 */
static void test_send_error_tolerated(void **state) {
    PlayerFixture *fx = *state;
    // Arrange
    create_fixture(fx);
    Kit_PlayerPlay(fx->player);
    assert_true(wait_for_data(fx));
    Kit_SetFailPoint("decode_send", 3, 1, AVERROR_INVALIDDATA);
    const int baseline = Kit_GetFailPointCount("decode_send");

    // Act / Assert: the injection provably fires, and frames keep arriving after it.
    // (wait_for_data alone could pass from already-buffered frames before the armed
    // ordinal is ever reached, making the test vacuous.)
    assert_true(wait_for_fail_point(fx, "decode_send", baseline, 3));
    assert_true(wait_for_data(fx));
    assert_true(wait_for_data(fx));

    close_fixture(fx);
}

// -- test_send_error_fatal_code ----------------------------------------

/**
 * @brief A single AVERROR(ENOMEM) injected on "decode_send" maps to KIT_DEC_INPUT_RETRY: the unconsumed packet is
 * transparently re-sent on the decoder thread's next pass, so playback is never interrupted.
 */
static void test_send_error_fatal_code(void **state) {
    PlayerFixture *fx = *state;
    // Arrange
    create_fixture(fx);
    Kit_PlayerPlay(fx->player);
    assert_true(wait_for_data(fx));
    Kit_SetFailPoint("decode_send", 3, 1, AVERROR(ENOMEM));
    const int baseline = Kit_GetFailPointCount("decode_send");

    // Act / Assert: the injection provably fires, and frames keep arriving after it
    // (transparent retry). See test_send_error_tolerated for the vacuous-pass concern.
    assert_true(wait_for_fail_point(fx, "decode_send", baseline, 3));
    assert_true(wait_for_data(fx));
    assert_true(wait_for_data(fx));

    close_fixture(fx);
}

// -- test_send_eof_ends_stream -----------------------------------------

/**
 * @brief AVERROR_EOF injected on "decode_send" hits the explicit KIT_DEC_INPUT_EOF case in
 * dec_input_{audio,video}_cb, ending that decoder's stream cleanly instead of being skipped. Pins the audio
 * path's case in particular: it used to be the unreachable `case AVERROR(EOF)` (evaluates to +1),
 * so an audio-side send EOF fell into the "skip errors" default. Whichever decoder consumes the injection winds
 * down; overall playback still drains to a clean stop.
 */
static void test_send_eof_ends_stream(void **state) {
    PlayerFixture *fx = *state;
    // Arrange: small input packet buffers keep the demuxer mid-file when the fault fires below,
    // so the early-ended stream's packets keep coming and the wind-down path is actually
    // exercised (with default buffers a fast machine buffers the whole fixture before the fault
    // arms, and the mid-stream EOF case degenerates into the normal end-of-file one).
    Kit_PlayerConfig config;
    Kit_ResetPlayerConfig(&config);
    config.video.packet_buffer_size = 4;
    config.audio.packet_buffer_size = 4;
    create_fixture_config(fx, &config);
    Kit_PlayerPlay(fx->player);
    assert_true(wait_for_data(fx));

    // Act: the next packet send on either decoder thread consumes the injected EOF.
    Kit_SetFailPoint("decode_send", 1, 1, AVERROR_EOF);

    // Assert: the ended stream stops feeding and playback winds down to the lazy STOPPED flip --
    // a wedged/starved pipeline would drain to idle too, but never reach KIT_STOPPED.
    assert_true(drain_to_idle(fx, 10));
    assert_true(wait_for_stopped(fx->player));

    close_fixture(fx);
}

// -- test_receive_error_tolerated --------------------------------------

/**
 * @brief A single AVERROR_INVALIDDATA injected on "decode_receive" (in place of avcodec_receive_frame) is treated
 * like "no frame available this pass": dec_decode_{audio,video}_cb special-case only 0 and AVERROR_EOF, so any
 * other code just ends the decode pass; the decoder thread retries on its next iteration and playback continues.
 */
static void test_receive_error_tolerated(void **state) {
    PlayerFixture *fx = *state;
    // Arrange
    create_fixture(fx);
    Kit_PlayerPlay(fx->player);
    assert_true(wait_for_data(fx));
    Kit_SetFailPoint("decode_receive", 3, 1, AVERROR_INVALIDDATA);
    const int baseline = Kit_GetFailPointCount("decode_receive");

    // Act / Assert: the injection provably fires, and frames keep arriving after it.
    assert_true(wait_for_fail_point(fx, "decode_receive", baseline, 3));
    assert_true(wait_for_data(fx));
    assert_true(wait_for_data(fx));

    close_fixture(fx);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_send_error_tolerated, kit_playback_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_send_error_fatal_code, kit_playback_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_send_eof_ends_stream, kit_playback_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_receive_error_tolerated, kit_playback_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}

#else

int main(void) {
    return 0;
}

#endif // KIT_FAULT_INJECTION
