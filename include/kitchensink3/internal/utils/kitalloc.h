#ifndef KITALLOC_H
#define KITALLOC_H

/**
 * @brief Allocation wrappers for test use.
 *
 * @file kitalloc.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <stdlib.h>

#include "kitchensink3/internal/kitfaultinject.h"

/**
 * @brief Allocates zero-initialized memory, wrapping calloc() with the "alloc" fault-injection point.
 *
 * In test/debug builds this can be forced to fail via the "alloc" fault injection point regardless of
 * available memory; in release (fault injection disabled) builds this is a plain call to calloc().
 *
 * @param nmemb Number of elements to allocate
 * @param size Size of each element, in bytes
 * @return Pointer to allocated zero-initialized memory, or NULL on failure (real or injected)
 */
static inline void *Kit_Calloc(size_t nmemb, size_t size) {
    if(KIT_FAIL_POINT("alloc"))
        return NULL;
    return calloc(nmemb, size);
}

/**
 * @brief Allocates memory, wrapping malloc() with the "alloc" fault-injection point.
 *
 * In test/debug builds this can be forced to fail via the "alloc" fault injection point regardless of
 * available memory; in release (fault injection disabled) builds this is a plain call to malloc().
 *
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure (real or injected)
 */
static inline void *Kit_Malloc(size_t size) {
    if(KIT_FAIL_POINT("alloc"))
        return NULL;
    return malloc(size);
}

#endif // KITALLOC_H
