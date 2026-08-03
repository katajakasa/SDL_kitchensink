/**
 * Assertion helpers that print the offending values on failure, unlike a
 * bare assert_true() on a comparison expression, which reports only the
 * expression text. Use these for checks whose failures need to be
 * diagnosable straight from a CI log.
 *
 * Each helper is a real function taking the caller's file/line, fronted by
 * a thin macro that forwards __FILE__/__LINE__ -- the same pattern cmocka's
 * own asserts use -- so failures are reported at the call site, not here.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */
#ifndef KIT_ASSERT_H
#define KIT_ASSERT_H

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <SDL3/SDL_rect.h>

#include "kitchensink3/kitchensink.h"

static inline void
assert_double_in_range_impl(const double value, const double min, const double max, const char *file, const int line) {
    if(value < min || value > max) {
        cmocka_print_error("%f not in range [%f, %f]\n", value, min, max);
        _fail(file, line);
    }
}

/** @brief Asserts min <= value <= max for doubles, reporting all three values on failure.
 * (cmocka's assert_int_in_range only handles integers.) */
#define assert_double_in_range(value, min, max) assert_double_in_range_impl((value), (min), (max), __FILE__, __LINE__)

static inline void assert_frect_in_bounds_impl(
    const SDL_FRect *rect, const float bound_w, const float bound_h, const char *file, const int line
) {
    if(rect->x < 0.0f || rect->y < 0.0f || rect->w <= 0.0f || rect->h <= 0.0f || rect->x + rect->w > bound_w ||
       rect->y + rect->h > bound_h) {
        cmocka_print_error(
            "rect {x=%f, y=%f, w=%f, h=%f} not within %fx%f bounds\n",
            rect->x,
            rect->y,
            rect->w,
            rect->h,
            bound_w,
            bound_h
        );
        _fail(file, line);
    }
}

/** @brief Asserts an SDL_FRect has a non-negative origin, a positive size, and lies fully inside a
 * bound_w x bound_h area, reporting the offending rect on failure. */
#define assert_frect_in_bounds(rect, bound_w, bound_h)                                                               \
    assert_frect_in_bounds_impl((rect), (bound_w), (bound_h), __FILE__, __LINE__)

static inline void assert_source_open_fails_cleanly_impl(const char *path, const char *file, const int line) {
    const char *printable_path = path != NULL ? path : "(null)";
    Kit_ClearError();
    Kit_Source *src = Kit_CreateSourceFromUrl(path);
    if(src != NULL) {
        Kit_CloseSource(src);
        cmocka_print_error("opening \"%s\" unexpectedly succeeded\n", printable_path);
        _fail(file, line);
    }
    if(Kit_GetError() == NULL) {
        cmocka_print_error("failed open of \"%s\" left no Kit_GetError() message\n", printable_path);
        _fail(file, line);
    }
}

/** @brief Asserts that opening `path` as a source fails cleanly: NULL return plus a non-NULL Kit_GetError(). */
#define assert_source_open_fails_cleanly(path) assert_source_open_fails_cleanly_impl((path), __FILE__, __LINE__)

#endif // KIT_ASSERT_H
