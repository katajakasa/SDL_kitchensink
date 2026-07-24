#ifndef KITERROR_H
#define KITERROR_H

/**
 * @brief Error handling functions
 *
 * Error messages are stored per thread: an error set by a library call is only visible to
 * Kit_GetError() on the thread that made the failing call.
 *
 * @file kiterror.h
 * @author Tuomas Virtanen
 * @date 2018-06-25
 */

#include "kitchensink2/kitconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns the latest error of the calling thread. This is set by SDL_kitchensink library
 * functions on error.
 *
 * @return Error message or NULL
 */
KIT_API const char *Kit_GetError();

/**
 * @brief Sets the error message for the calling thread. This should really only be used by the library.
 *
 * @param fmt Message format
 * @param ... Message arguments
 */
KIT_API void Kit_SetError(const char *fmt, ...);

/**
 * @brief Clears the calling thread's error message. After this, Kit_GetError() will return NULL.
 */
KIT_API void Kit_ClearError();

#ifdef __cplusplus
}
#endif

#endif // KITERROR_H
