#ifndef KITLIBSTATE_H
#define KITLIBSTATE_H

/**
 * @brief Process-wide singleton holding SDL_kitchensink's init flags and libass handles.
 *
 * @file kitlibstate.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/internal/libass.h"
#include "kitchensink2/kitconfig.h"

/**
 * @brief Global library state: init flags and libass handles. All tuning lives in the
 * per-player Kit_PlayerConfig. There is exactly one static instance, accessed via
 * Kit_GetLibraryState().
 */
typedef struct Kit_LibraryState {
    unsigned int init_flags;
    ASS_Library *libass_handle;
    void *ass_so_handle;
} Kit_LibraryState;

/**
 * @brief Gets a pointer to the single process-wide library state instance.
 *
 * @return Pointer to the static Kit_LibraryState instance (never NULL)
 */
KIT_LOCAL Kit_LibraryState *Kit_GetLibraryState();

#endif // KITLIBSTATE_H
