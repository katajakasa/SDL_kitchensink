/**
 * Threaded unit test for Kit_Timer (kittimer.h): a primary timer is written
 * from a background thread while the main thread concurrently polls a
 * secondary, read-only timer sharing the same value block, so TSan can
 * validate the mutex/atomic-guarded shared fields have no unsynchronized access.
 * See test_timer.c for single-threaded API coverage (not repeated here).
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include "kitchensink3/internal/kittimer.h"

#define WRITER_DURATION_MS 200

typedef struct writer_ctx {
    Kit_Timer *timer;
    unsigned int final_serial;
} writer_ctx;

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or strand a live writer thread. The writer
 * context lives here too: heap-allocated state stays valid for a still-running writer even
 * after an assert longjmps out of the test body's frame. */
typedef struct {
    Kit_Timer *primary;
    Kit_Timer *secondary;
    SDL_Thread *thread;
    writer_ctx ctx;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: joins any writer thread a mid-test assert failure left running
 * (its loop is bounded to ~WRITER_DURATION_MS, so the join returns promptly), then closes
 * both timers and frees the state, so a failed test cannot strand a live thread or leak
 * the timers. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    if(ts->thread != NULL) {
        SDL_WaitThread(ts->thread, NULL);
        ts->thread = NULL;
    }
    Kit_CloseTimer(&ts->secondary); // NULL-safe; NULLs the pointer
    Kit_CloseTimer(&ts->primary);
    free(ts);
    *state = NULL;
    return 0;
}

/**
 * @brief Writer thread body: repeatedly nudges the primary timer's base backwards and bumps the serial.
 * Never adds a positive delta, so wall-clock progress and the base shift both push elapsed the same direction.
 */
static int writer_thread(void *data) {
    writer_ctx *ctx = data;
    const uint32_t start = SDL_GetTicks();
    unsigned int serial = 0;
    while(SDL_GetTicks() - start < WRITER_DURATION_MS) {
        Kit_AddTimerBase(ctx->timer, -0.0005);
        serial = Kit_IncreaseTimerSerial(ctx->timer);
        SDL_Delay(1);
    }
    ctx->final_serial = serial;
    return 0;
}

/**
 * @brief While a writer thread advances the primary timer's base/serial, a secondary timer's elapsed time and serial
 * must stay monotonically non-decreasing.
 */
static void test_secondary_timer_cross_thread(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->primary = Kit_CreateTimer();
    Kit_SetTimerBase(ts->primary);
    ts->secondary = Kit_CreateSecondaryTimer(ts->primary, false);
    assert_non_null(ts->secondary);
    ts->ctx = (writer_ctx){.timer = ts->primary, .final_serial = 0};

    // Act: writer advances the primary while the main thread polls the secondary
    ts->thread = SDL_CreateThread(writer_thread, "timer_mt_writer", &ts->ctx);
    assert_non_null(ts->thread);

    double last_elapsed = Kit_GetTimerElapsed(ts->secondary);
    unsigned int last_serial = Kit_GetTimerSerial(ts->secondary);
    const uint32_t poll_start = SDL_GetTicks();
    while(SDL_GetTicks() - poll_start < WRITER_DURATION_MS + 50) {
        const double elapsed = Kit_GetTimerElapsed(ts->secondary);
        const unsigned int serial = Kit_GetTimerSerial(ts->secondary);

        // Assert: monotonic non-decreasing elapsed and serial
        assert_true(elapsed >= last_elapsed);
        assert_true(serial >= last_serial);

        last_elapsed = elapsed;
        last_serial = serial;
    }

    int writer_status = -1;
    SDL_WaitThread(ts->thread, &writer_status);
    ts->thread = NULL;
    assert_int_equal(writer_status, 0);

    // Assert: the secondary observed the writer's final serial (no writes lost)
    assert_true(last_serial <= ts->ctx.final_serial);
    assert_int_equal(Kit_GetTimerSerial(ts->secondary), ts->ctx.final_serial);

    Kit_CloseTimer(&ts->secondary);
    Kit_CloseTimer(&ts->primary);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_secondary_timer_cross_thread, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
