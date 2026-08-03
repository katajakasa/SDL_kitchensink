/**
 * Shared probe-then-sweep machinery for fault-point ordinal sweeps: probe how
 * many times a constructor consults a fail point when unarmed, then arm each
 * ordinal in turn and let a caller-supplied attempt function assert that the
 * construction fails cleanly. Only usable in KIT_FAULT_INJECTION builds.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */
#ifndef KIT_FAULTSWEEP_H
#define KIT_FAULTSWEEP_H

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "kitchensink3/internal/kitfaultinject.h"
#include "kitchensink3/kiterror.h"

/** @brief Runs fn(ctx) once with no fail points armed and returns how many `name` checks it consumed. */
static inline int kit_probe_fail_point_count(const char *name, void (*fn)(void *ctx), void *ctx) {
    Kit_ResetFailPoints();
    fn(ctx);
    const int count = Kit_GetFailPointCount(name);
    Kit_ResetFailPoints();
    return count;
}

/** @brief Arms `name` at each ordinal 1..ordinals in turn and calls attempt(ctx), which must perform
 * the construction and assert that it failed (and may assert its own error-message policy).
 * Returns how many ordinals left no error message set.
 *
 * Only PTR-form fail points (KIT_FAULT_WRAP_PTR / Kit_TestFailPoint) can be swept: the point is
 * armed with error_code 0, which a CODE-form wrap would return as a fake *success* without ever
 * executing the wrapped call. Also note that an assert failure inside attempt() longjmps past the
 * resets below, so the caller's test teardown must call Kit_ResetFailPoints() itself (all current
 * callers do). */
static inline int kit_sweep_fail_point(const char *name, int ordinals, void (*attempt)(void *ctx), void *ctx) {
    int no_error_msg_count = 0;
    for(int i = 1; i <= ordinals; i++) {
        Kit_ResetFailPoints();
        Kit_ClearError();
        Kit_SetFailPoint(name, i, 1, 0);
        attempt(ctx);
        const char *err = Kit_GetError();
        if(err == NULL || strlen(err) == 0)
            no_error_msg_count++;
    }
    Kit_ResetFailPoints();
    return no_error_msg_count;
}

#endif // KIT_FAULTSWEEP_H
