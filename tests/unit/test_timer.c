/**
 * Unit tests for Kit_Timer (kittimer.h): the pausable, serial-tracked clock
 * used to drive audio/video sync. Covers primary timers and secondary
 * timers that share a primary's refcounted base value.
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

#include "kit_assert.h"

#include "kitchensink3/internal/kittimer.h"

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or cascade into the remaining tests in the
 * group. Kit_CloseTimer() NULLs the pointer itself, so each test's own close already resets
 * these members. */
typedef struct {
    Kit_Timer *timer;
    Kit_Timer *secondary;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: releases whatever the TestState still holds, then the state
 * itself. Kit_CloseTimer() is NULL-safe and the shared value block is refcounted, so closing
 * the secondary and primary in either order (or only one of them) is always safe. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    Kit_CloseTimer(&ts->secondary);
    Kit_CloseTimer(&ts->timer);
    free(ts);
    *state = NULL;
    return 0;
}

/**
 * @brief A fresh timer is primary and uninitialized; Kit_CloseTimer() nulls the pointer and tolerates a double close.
 */
static void test_create_and_close(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->timer = Kit_CreateTimer();

    // Act / Assert: creation state
    assert_non_null(ts->timer);
    assert_false(Kit_IsTimerInitialized(ts->timer));
    assert_true(Kit_IsTimerPrimary(ts->timer));

    // Act / Assert: close nulls the pointer, and closing again is a no-op
    Kit_CloseTimer(&ts->timer);
    assert_null(ts->timer);
    Kit_CloseTimer(&ts->timer); // NULL-safe double close
}

/**
 * @brief Kit_InitTimerBase()/Kit_ResetTimerBase() flip the initialized flag on and off.
 */
static void test_init_and_reset(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->timer = Kit_CreateTimer();

    // Act / Assert: init flips the flag on
    Kit_InitTimerBase(ts->timer);
    assert_true(Kit_IsTimerInitialized(ts->timer));

    // Act / Assert: reset flips it back off
    Kit_ResetTimerBase(ts->timer);
    assert_false(Kit_IsTimerInitialized(ts->timer));

    Kit_CloseTimer(&ts->timer);
}

/**
 * @brief An uninitialized timer reads elapsed 0.0, and Kit_ResetTimerBase() returns the reading to 0.0.
 */
static void test_elapsed_zero_when_uninitialized(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->timer = Kit_CreateTimer();

    // Act / Assert: uninitialized timer reads 0.0
    assert_true(Kit_GetTimerElapsed(ts->timer) == 0.0);

    // Act / Assert: reset returns an initialized timer's reading to 0.0
    Kit_SetTimerBase(ts->timer);
    Kit_AddTimerBase(ts->timer, -5.0);
    assert_true(Kit_GetTimerElapsed(ts->timer) >= 5.0);
    Kit_ResetTimerBase(ts->timer);
    assert_true(Kit_GetTimerElapsed(ts->timer) == 0.0);

    Kit_CloseTimer(&ts->timer);
}

/**
 * @brief Right after Kit_SetTimerBase(), elapsed time is a small non-negative value.
 */
static void test_elapsed_starts_near_zero(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->timer = Kit_CreateTimer();
    Kit_SetTimerBase(ts->timer);

    // Act
    const double elapsed = Kit_GetTimerElapsed(ts->timer);

    // Assert
    assert_double_in_range(elapsed, 0.0, 1.0);
    Kit_CloseTimer(&ts->timer);
}

/**
 * @brief Kit_AddTimerBase() shifts the effective base backwards, showing up immediately as added elapsed time.
 */
static void test_add_base_shifts_elapsed(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->timer = Kit_CreateTimer();
    Kit_SetTimerBase(ts->timer);
    Kit_AddTimerBase(ts->timer, -5.0);

    // Act
    const double elapsed = Kit_GetTimerElapsed(ts->timer);

    // Assert
    assert_double_in_range(elapsed, 5.0, 6.0);
    Kit_CloseTimer(&ts->timer);
}

/**
 * @brief While paused, elapsed time reads as a frozen, identical value; resuming lets it advance again.
 */
static void test_pause_freezes_elapsed(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->timer = Kit_CreateTimer();
    Kit_SetTimerBase(ts->timer);
    Kit_PauseTimer(ts->timer);

    // Act / Assert: paused reads are frozen
    const double e1 = Kit_GetTimerElapsed(ts->timer);
    const double e2 = Kit_GetTimerElapsed(ts->timer);
    assert_true(e1 == e2); // frozen while paused

    // Act / Assert: resuming lets elapsed advance again
    Kit_ResumeTimer(ts->timer);
    assert_true(Kit_GetTimerElapsed(ts->timer) >= e2);
    Kit_CloseTimer(&ts->timer);
}

/**
 * @brief Bumping the serial desyncs the timer until Kit_SetTimerBaseSerial() catches the base serial back up.
 */
static void test_serials(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->timer = Kit_CreateTimer();

    // Act / Assert: a fresh timer is synced at serial 0
    assert_int_equal(Kit_GetTimerSerial(ts->timer), 0);
    assert_true(Kit_IsTimerSynced(ts->timer));

    // Act / Assert: bumping the serial desyncs the timer
    assert_int_equal(Kit_IncreaseTimerSerial(ts->timer), 1);
    assert_int_equal(Kit_GetTimerSerial(ts->timer), 1);
    assert_false(Kit_IsTimerSynced(ts->timer));

    // Act / Assert: catching the base serial up resyncs it
    Kit_SetTimerBaseSerial(ts->timer, 1);
    assert_true(Kit_IsTimerSynced(ts->timer));
    Kit_CloseTimer(&ts->timer);
}

/**
 * @brief A secondary timer shares its primary's refcounted base value, and that block outlives whichever timer closes
 * first.
 */
static void test_secondary_timer_shares_state(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->timer = Kit_CreateTimer();
    ts->secondary = Kit_CreateSecondaryTimer(ts->timer, false);
    assert_non_null(ts->secondary);

    // Act
    Kit_SetTimerBase(ts->timer);

    // Assert: shares state with primary
    assert_true(Kit_IsTimerInitialized(ts->secondary));

    // Close order: value block is refcounted, either order works
    Kit_CloseTimer(&ts->timer);
    assert_true(Kit_IsTimerInitialized(ts->secondary));
    Kit_CloseTimer(&ts->secondary);
}

/**
 * @brief A non-writeable secondary timer reports as non-primary and cannot mutate the shared base.
 */
static void test_secondary_timer_not_writeable(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->timer = Kit_CreateTimer();
    ts->secondary = Kit_CreateSecondaryTimer(ts->timer, false);
    assert_non_null(ts->secondary);
    Kit_SetTimerBase(ts->timer);

    // Act
    Kit_ResetTimerBase(ts->secondary);

    // Assert
    assert_false(Kit_IsTimerPrimary(ts->secondary));
    assert_true(Kit_IsTimerInitialized(ts->timer));

    Kit_CloseTimer(&ts->secondary);
    Kit_CloseTimer(&ts->timer);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_create_and_close, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_init_and_reset, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_elapsed_zero_when_uninitialized, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_elapsed_starts_near_zero, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_add_base_shifts_elapsed, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_pause_freezes_elapsed, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_serials, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_secondary_timer_shares_state, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_secondary_timer_not_writeable, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
