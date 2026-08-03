/**
 * Tests for the public Kit_SetError/Kit_GetError/Kit_ClearError thread-local
 * error string API (kiterror.h). No Kit_Init() is required since the
 * error state is independent of library init.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "kitchensink2/kitchensink.h"

/**
 * @brief Kit_SetError/Kit_GetError/Kit_ClearError round-trip through clear -> set -> read -> clear.
 * Kit_GetError() is one-shot (a read consumes the error), so the final clear is pinned by re-arming
 * a fresh, unread error first.
 */
static void test_error_lifecycle(void **state) {
    (void)state;
    // Arrange: start from a clean slate.
    Kit_ClearError();
    assert_null(Kit_GetError());

    // Act
    Kit_SetError("problem %d in %s", 42, "unit");

    // Assert
    const char *error = Kit_GetError();
    assert_non_null(error);
    assert_string_equal(error, "problem 42 in unit");

    // Assert
    // Reading is one-shot: the error above was consumed by the read.
    assert_null(Kit_GetError());

    // Act / Assert
    // Kit_ClearError() drops an unread error.
    Kit_SetError("to be cleared");
    Kit_ClearError();
    assert_null(Kit_GetError());
}

/**
 * @brief A new Kit_SetError() call must replace the previous error, not append.
 */
static void test_error_overwrite(void **state) {
    (void)state;
    // Arrange
    Kit_SetError("first");

    // Act
    Kit_SetError("second");

    // Assert
    assert_string_equal(Kit_GetError(), "second");
    Kit_ClearError();
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_error_lifecycle),
        cmocka_unit_test(test_error_overwrite),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
