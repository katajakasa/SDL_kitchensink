#ifndef KITLOG_H
#define KITLOG_H

/**
 * @brief Internal debug logging macro; compiles away to nothing in NDEBUG (release) builds.
 *
 * @file kitlog.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#ifdef NDEBUG
#define LOG(...)
#else
#include <stdio.h>
#define LOG(...)                                                                                                      \
    fprintf(stderr, __VA_ARGS__);                                                                                     \
    fflush(stderr)
#endif

#endif // KITLOG_H
