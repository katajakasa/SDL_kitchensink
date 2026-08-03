/**
 * Threaded tests for the thread-local Kit_SetError/Kit_GetError/Kit_ClearError
 * API (kiterror.h); test_error.c covers the single-thread lifecycle.
 * Kit_GetError() is one-shot (reading clears the flag), so each worker reads
 * its own error exactly once and reports back via its ctx struct. No cmocka
 * asserts on worker threads (cmocka's longjmp machinery is not thread-safe);
 * assertions run after SDL_WaitThread() on the main thread.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <string.h>

#include <SDL_thread.h>

#include "kitchensink2/kitchensink.h"

/** @brief Sets a distinct error on this thread, then reports what Kit_GetError() reads back via `data`. */
static int set_error_thread(void *data) {
    char *observed = data;
    Kit_SetError("thread");
    const char *error = Kit_GetError();
    if(error != NULL)
        strncpy(observed, error, 63);
    return 0;
}

/**
 * @brief Kit_SetError() is thread-local: a background thread's error must not be visible
 * on the main thread, and the main thread's own error must survive untouched.
 */
static void test_error_is_thread_local(void **state) {
    (void)state;
    // Arrange
    Kit_ClearError();
    Kit_SetError("main");
    char observed_by_thread[64] = {0};

    // Act: background thread sets and reads its own, distinct error
    SDL_Thread *thread = SDL_CreateThread(set_error_thread, "error_mt_setter", observed_by_thread);
    assert_non_null(thread);
    SDL_WaitThread(thread, NULL);

    // Assert: each thread only ever saw its own error
    assert_string_equal(observed_by_thread, "thread");
    assert_string_equal(Kit_GetError(), "main");

    Kit_ClearError();
}

/** @brief Sets then clears this thread's own error, reporting via `data` whether it's NULL afterwards. */
static int clear_error_thread(void *data) {
    bool *observed_null = data;
    Kit_SetError("thread error to be cleared");
    Kit_ClearError();
    *observed_null = (Kit_GetError() == NULL);
    return 0;
}

/**
 * @brief Kit_ClearError() is thread-local: a background thread clearing its own error
 * must have no effect on an error already set on the main thread.
 */
static void test_clear_error_is_thread_local(void **state) {
    (void)state;
    // Arrange
    Kit_ClearError();
    Kit_SetError("main error, must survive");
    bool observed_null_by_thread = false;

    // Act: background thread sets then clears its own error
    SDL_Thread *thread = SDL_CreateThread(clear_error_thread, "error_mt_clearer", &observed_null_by_thread);
    assert_non_null(thread);
    SDL_WaitThread(thread, NULL);

    // Assert: the thread's clear worked locally, main's error is unaffected
    assert_true(observed_null_by_thread);
    assert_string_equal(Kit_GetError(), "main error, must survive");

    Kit_ClearError();
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_error_is_thread_local),
        cmocka_unit_test(test_clear_error_is_thread_local),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
