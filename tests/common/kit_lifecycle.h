/**
 * Shared cmocka group setup/teardown pairs: every test group brings the
 * library (and usually SDL video) up once for the whole group. Files that
 * need extra pre-work (config resets, fail-point resets) keep a local
 * wrapper that does its own bit and then calls one of these.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */
#ifndef KIT_LIFECYCLE_H
#define KIT_LIFECYCLE_H

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <SDL.h>

#include "kitchensink2/kitchensink.h"

/** @brief Group setup: initialize the library only (no SDL); pairs with kit_lifecycle_teardown(). */
static inline int kit_lifecycle_setup(void **state) {
    (void)state;
    return Kit_Init(0) == 0 ? 0 : -1;
}

/** @brief Group teardown for kit_lifecycle_setup(): shut down the library. */
static inline int kit_lifecycle_teardown(void **state) {
    (void)state;
    Kit_Quit();
    return 0;
}

/** @brief Group setup: init SDL video and the library; pairs with kit_lifecycle_teardown_video(). */
static inline int kit_lifecycle_setup_video(void **state) {
    (void)state;
    if(SDL_Init(SDL_INIT_VIDEO) != 0)
        return -1;
    if(Kit_Init(0) != 0) {
        SDL_Quit(); // cmocka skips group teardown when setup fails
        return -1;
    }
    return 0;
}

/** @brief Group setup: init SDL video and the library with KIT_INIT_ASS; pairs with
 * kit_lifecycle_teardown_video(). */
static inline int kit_lifecycle_setup_video_ass(void **state) {
    (void)state;
    if(SDL_Init(SDL_INIT_VIDEO) != 0)
        return -1;
    if(Kit_Init(KIT_INIT_ASS) != 0) {
        SDL_Quit(); // cmocka skips group teardown when setup fails
        return -1;
    }
    return 0;
}

/** @brief Group teardown for the _video setup variants: shut down the library and SDL. */
static inline int kit_lifecycle_teardown_video(void **state) {
    (void)state;
    Kit_Quit();
    SDL_Quit();
    return 0;
}

#endif // KIT_LIFECYCLE_H
