/**
 * Unit tests for kithelpers.h: small pure math helpers (Kit_max/Kit_min/
 * Kit_clamp).
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "kitchensink3/internal/utils/kithelpers.h"

/**
 * @brief Kit_max() picks the larger value regardless of argument order, including negative numbers and ties.
 */
static void test_kit_max(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_int_equal(Kit_max(1, 2), 2);
    assert_int_equal(Kit_max(2, 1), 2);
    assert_int_equal(Kit_max(-5, -9), -5);
    assert_int_equal(Kit_max(3, 3), 3);
}

/**
 * @brief Kit_min() picks the smaller value regardless of argument order, including negative numbers and ties.
 */
static void test_kit_min(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_int_equal(Kit_min(1, 2), 1);
    assert_int_equal(Kit_min(2, 1), 1);
    assert_int_equal(Kit_min(-5, -9), -9);
    assert_int_equal(Kit_min(3, 3), 3);
}

/**
 * @brief Kit_clamp() passes through in-range values and saturates at the inclusive bounds otherwise.
 */
static void test_kit_clamp(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_int_equal(Kit_clamp(5, 0, 10), 5);
    assert_int_equal(Kit_clamp(-1, 0, 10), 0);
    assert_int_equal(Kit_clamp(11, 0, 10), 10);
    assert_int_equal(Kit_clamp(0, 0, 10), 0);
    assert_int_equal(Kit_clamp(10, 0, 10), 10);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_kit_max),
        cmocka_unit_test(test_kit_min),
        cmocka_unit_test(test_kit_clamp),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
