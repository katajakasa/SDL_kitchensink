/**
 * Helper for parametrized cmocka tests: registers the same test function
 * several times with different parameter structs, each reported as
 * "base_name[case_label]" so a single failing case is identifiable.
 * Storage for the generated names must outlive cmocka_run_group_tests()
 * (declare the KitParamName array in main()'s scope).
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */
#ifndef KIT_PARAM_H
#define KIT_PARAM_H

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <stdio.h>

#define KIT_PARAM_MAX_NAME 96

typedef struct KitParamName {
    char name[KIT_PARAM_MAX_NAME];
} KitParamName;

static inline struct CMUnitTest kit_param_test(
    KitParamName *storage,
    const char *base_name,
    const char *case_label,
    CMUnitTestFunction test_func,
    CMFixtureFunction setup_func,
    CMFixtureFunction teardown_func,
    void *param
) {
    snprintf(storage->name, KIT_PARAM_MAX_NAME, "%s[%s]", base_name, case_label);
    const struct CMUnitTest test = {storage->name, test_func, setup_func, teardown_func, param};
    return test;
}

#endif // KIT_PARAM_H
