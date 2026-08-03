#ifndef KITFAULTINJECT_H
#define KITFAULTINJECT_H

/**
 * @brief Debug-only fault-injection registry for test use.
 *
 * @file kitfaultinject.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink3/kitconfig.h"

#ifdef KIT_FAULT_INJECTION

#include <stdbool.h>

#define KIT_FAIL_POINT(name) Kit_TestFailPoint(name)
#define KIT_FAIL_POINT_CODE(name, errp) Kit_TestFailPointCode(name, errp)

// CODE form substitutes the armed error code for an int-returning call; PTR form substitutes NULL for a pointer-returning call.
// Both evaluate call at most once; CODE form is GCC/Clang-only (statement expression).
#define KIT_FAULT_WRAP_CODE(name, call) __extension__ ({ \
        int kit_fw_err_ = 0; \
        KIT_FAIL_POINT_CODE(name, &kit_fw_err_) ? kit_fw_err_ : (call); \
    })
#define KIT_FAULT_WRAP_PTR(name, call) (KIT_FAIL_POINT(name) ? NULL : (call))

/**
 * @brief Checks (and counts) a call to the named fail point; use for pointer-returning calls.
 *
 * @param name Fail point name (registry entry is created on first use)
 * @return true if the fail point is currently armed for this call, false otherwise
 */
KIT_API bool Kit_TestFailPoint(const char *name);
/**
 * @brief Checks (and counts) a call to the named fail point; use for int-returning calls.
 *
 * @param name Fail point name (registry entry is created on first use)
 * @param error_code Set to the armed error code if the fail point fires; untouched otherwise. May be NULL.
 * @return true if the fail point is currently armed for this call, false otherwise
 */
KIT_API bool Kit_TestFailPointCode(const char *name, int *error_code);
/**
 * @brief Arms a fail point so a window of future calls to it will fail.
 *
 * @param name Fail point name (registry entry is created on first use). Must be a string
 *             literal or otherwise outlive the registry: the pointer is stored, not copied.
 * @param first_failing_call 1-based index, counted from this arming, of the first check that
 *                           fails: 1 fails the very next check, 3 lets two checks through first
 * @param count Number of consecutive calls to fail once the window starts; -1 fails forever
 * @param error_code Error code delivered via Kit_TestFailPointCode() while armed
 */
KIT_API void Kit_SetFailPoint(const char *name, int first_failing_call, int count, int error_code);
/**
 * @brief Clears the entire fail point registry, removing all entries and counters.
 */
KIT_API void Kit_ResetFailPoints(void);
/**
 * @brief Gets the lifetime number of times the named fail point has been checked.
 *
 * @param name Fail point name
 * @return Number of checks recorded, or 0 if the name has never been seen
 */
KIT_API int Kit_GetFailPointCount(const char *name);

#else

#define KIT_FAIL_POINT(name) (0)
#define KIT_FAIL_POINT_CODE(name, errp) (0)

#define KIT_FAULT_WRAP_CODE(name, call) (call)
#define KIT_FAULT_WRAP_PTR(name, call) (call)

#endif // KIT_FAULT_INJECTION

#endif // KITFAULTINJECT_H
