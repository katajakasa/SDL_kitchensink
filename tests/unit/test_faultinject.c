/**
 * Unit tests for the debug-only fault-injection registry (kitfaultinject.h):
 * registry semantics only (arming windows, counters, thread safety). No
 * media, no Kit_Init. Built only when KIT_FAULT_INJECTION is enabled; the
 * #else branch keeps the binary buildable/runnable (empty) otherwise.
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

#include <SDL3/SDL_thread.h>

#include "kitchensink3/internal/kitfaultinject.h"

#define WORKER_ITERATIONS 10000

typedef struct worker_ctx {
    int fired;
} worker_ctx;

/** @brief Test lifecycle setup: reset fail points so the suite starts with a clean registry. */
static int group_setup(void **state) {
    (void)state;
    Kit_ResetFailPoints();
    return 0;
}

/** @brief Test lifecycle teardown: reset fail points so no armings leak past this suite. */
static int group_teardown(void **state) {
    (void)state;
    Kit_ResetFailPoints();
    return 0;
}

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown().
 * Only test_concurrent_checks touches it; the worker contexts live here because heap-allocated
 * state stays valid for a still-running worker even after an assert longjmps out of the test
 * body's frame. */
typedef struct {
    worker_ctx ctx_a;
    worker_ctx ctx_b;
    SDL_Thread *thread_a;
    SDL_Thread *thread_b;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: joins any worker thread a mid-test assert failure left running
 * (the worker loops are bounded to WORKER_ITERATIONS, so the joins return promptly), resets
 * the registry so armings (incl. count == -1 ones) and counters cannot leak between tests --
 * correctness must not depend on unique point names -- and frees the state. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    if(ts->thread_a != NULL) {
        SDL_WaitThread(ts->thread_a, NULL);
        ts->thread_a = NULL;
    }
    if(ts->thread_b != NULL) {
        SDL_WaitThread(ts->thread_b, NULL);
        ts->thread_b = NULL;
    }
    Kit_ResetFailPoints();
    free(ts);
    *state = NULL;
    return 0;
}

/**
 * @brief An unarmed point never fails and its lifetime check counter tracks every call.
 */
static void test_unarmed_never_fails(void **state) {
    (void)state;
    // Act
    for(int i = 0; i < 100; i++) {
        assert_false(KIT_FAIL_POINT("unarmed_point"));
    }

    // Assert
    assert_int_equal(Kit_GetFailPointCount("unarmed_point"), 100);
}

/**
 * @brief Arming (after 3, count 2) fails only checks 3-4 since arming; checks before and after pass.
 */
static void test_window_arms_and_disarms(void **state) {
    (void)state;
    // Arrange
    Kit_SetFailPoint("windowed_point", 3, 2, 0);

    // Act / Assert: checks 1-2 pass, 3-4 fail, 5+ pass
    assert_false(KIT_FAIL_POINT("windowed_point"));
    assert_false(KIT_FAIL_POINT("windowed_point"));
    assert_true(KIT_FAIL_POINT("windowed_point"));
    assert_true(KIT_FAIL_POINT("windowed_point"));
    assert_false(KIT_FAIL_POINT("windowed_point"));
    assert_false(KIT_FAIL_POINT("windowed_point"));
}

/**
 * @brief A point armed with count == -1 fails every check until Kit_ResetFailPoints() clears it.
 */
static void test_fail_forever(void **state) {
    (void)state;
    // Arrange
    Kit_SetFailPoint("forever_point", 1, -1, 0);

    // Act / Assert: fails repeatedly
    for(int i = 0; i < 50; i++) {
        assert_true(KIT_FAIL_POINT("forever_point"));
    }

    // Act: reset
    Kit_ResetFailPoints();

    // Assert: disarmed after reset
    assert_false(KIT_FAIL_POINT("forever_point"));
}

/**
 * @brief KIT_FAIL_POINT_CODE writes the armed error code on a failing check and leaves it untouched on a passing one.
 */
static void test_error_code_delivery(void **state) {
    (void)state;
    // Arrange
    Kit_SetFailPoint("coded_point", 2, 1, 42);
    int error_code = -1;

    // Act: check 1 passes, code must be untouched
    assert_false(KIT_FAIL_POINT_CODE("coded_point", &error_code));
    assert_int_equal(error_code, -1);

    // Act: check 2 fails, code must be delivered
    assert_true(KIT_FAIL_POINT_CODE("coded_point", &error_code));
    assert_int_equal(error_code, 42);
}

/**
 * @brief An unknown name's count is 0, and arming a misspelled name never affects checks against the correct one.
 */
static void test_count_query_unknown_name(void **state) {
    (void)state;
    // Arrange
    Kit_SetFailPoint("correkt_name", 1, -1, 0);

    // Act / Assert: unseen name has count 0
    assert_int_equal(Kit_GetFailPointCount("never_seen"), 0);

    // Assert: the misspelled arming never fires against the intended name
    assert_false(KIT_FAIL_POINT("correct_name"));
    assert_int_equal(Kit_GetFailPointCount("correkt_name"), 0);
}

/**
 * @brief Kit_ResetFailPoints() zeroes both lifetime counters and any active armings.
 */
static void test_reset_clears_all(void **state) {
    (void)state;
    // Arrange
    Kit_SetFailPoint("reset_point", 1, -1, 0);
    KIT_FAIL_POINT("reset_point");
    KIT_FAIL_POINT("reset_point");

    // Act
    Kit_ResetFailPoints();

    // Assert: counter and arming both gone
    assert_int_equal(Kit_GetFailPointCount("reset_point"), 0);
    assert_false(KIT_FAIL_POINT("reset_point"));
    assert_int_equal(Kit_GetFailPointCount("reset_point"), 1);
}

/**
 * @brief Worker thread body: hammers the shared point WORKER_ITERATIONS times. No cmocka asserts here -- results are
 * passed back via worker_ctx and asserted after SDL_WaitThread.
 */
static int concurrent_worker(void *data) {
    worker_ctx *ctx = data;
    int fired = 0;
    for(int i = 0; i < WORKER_ITERATIONS; i++) {
        if(KIT_FAIL_POINT("concurrent_point")) {
            fired++;
        }
    }
    ctx->fired = fired;
    return 0;
}

/**
 * @brief Two threads hammering the same unarmed point concurrently accumulate an exact combined lifetime count, with
 * no lost updates (TSan-relevant).
 */
static void test_concurrent_checks(void **state) {
    TestState *ts = *state;
    // Arrange. The contexts live in the heap-allocated state: the workers keep writing into
    // them, so a failed assert's longjmp out of this frame must not dangle their memory.
    ts->ctx_a = (worker_ctx){.fired = 0};
    ts->ctx_b = (worker_ctx){.fired = 0};

    // Act. Assert thread_a's handle before creating thread_b, so a failed create longjmps
    // with at most one live worker -- which the teardown then joins via its stored handle.
    ts->thread_a = SDL_CreateThread(concurrent_worker, "faultinject_a", &ts->ctx_a);
    assert_non_null(ts->thread_a);
    ts->thread_b = SDL_CreateThread(concurrent_worker, "faultinject_b", &ts->ctx_b);
    assert_non_null(ts->thread_b);

    int status_a = -1;
    int status_b = -1;
    SDL_WaitThread(ts->thread_a, &status_a);
    ts->thread_a = NULL;
    SDL_WaitThread(ts->thread_b, &status_b);
    ts->thread_b = NULL;

    // Assert
    assert_int_equal(status_a, 0);
    assert_int_equal(status_b, 0);
    assert_int_equal(ts->ctx_a.fired, 0);
    assert_int_equal(ts->ctx_b.fired, 0);
    assert_int_equal(Kit_GetFailPointCount("concurrent_point"), 2 * WORKER_ITERATIONS);
}

/**
 * @brief KIT_FAULT_WRAP_CODE returns the armed code on a failing check and the call result otherwise.
 */
static void test_wrap_code_substitutes(void **state) {
    (void)state;
    // Arrange
    Kit_SetFailPoint("wrap_code", 2, 1, -1234);

    // Act
    int first = KIT_FAULT_WRAP_CODE("wrap_code", 7);
    int second = KIT_FAULT_WRAP_CODE("wrap_code", 7);
    int third = KIT_FAULT_WRAP_CODE("wrap_code", 7);

    // Assert: only the armed check substitutes the code
    assert_int_equal(first, 7);
    assert_int_equal(second, -1234);
    assert_int_equal(third, 7);
}

/**
 * @brief KIT_FAULT_WRAP_PTR returns NULL on a failing check and the call result otherwise.
 */
static void test_wrap_ptr_substitutes(void **state) {
    (void)state;
    // Arrange
    static int target;
    Kit_SetFailPoint("wrap_ptr", 1, 1, 0);

    // Act
    int *first = KIT_FAULT_WRAP_PTR("wrap_ptr", &target);
    int *second = KIT_FAULT_WRAP_PTR("wrap_ptr", &target);

    // Assert: only the armed check substitutes NULL
    assert_null(first);
    assert_ptr_equal(second, &target);
}

/** @brief Increments the counter; wrapped by the tests below to detect whether the call was evaluated. */
static int bump(int *counter) {
    (*counter)++;
    return 0;
}

/**
 * @brief Both wrap macros skip evaluating the wrapped call on a failing check.
 */
static void test_wrap_skips_call_on_failure(void **state) {
    (void)state;
    // Arrange
    int code_calls = 0, ptr_calls = 0;
    Kit_SetFailPoint("wrap_skip_c", 1, -1, -1);
    Kit_SetFailPoint("wrap_skip_p", 1, -1, 0);

    // Act
    (void)KIT_FAULT_WRAP_CODE("wrap_skip_c", bump(&code_calls));
    int *p = KIT_FAULT_WRAP_PTR("wrap_skip_p", (bump(&ptr_calls), &ptr_calls));

    // Assert: neither wrapped call was evaluated
    assert_int_equal(code_calls, 0);
    assert_int_equal(ptr_calls, 0);
    assert_null(p);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_unarmed_never_fails, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_window_arms_and_disarms, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_fail_forever, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_error_code_delivery, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_count_query_unknown_name, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_reset_clears_all, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_concurrent_checks, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_wrap_code_substitutes, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_wrap_ptr_substitutes, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_wrap_skips_call_on_failure, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}

#else

int main(void) {
    return 0;
}

#endif // KIT_FAULT_INJECTION
