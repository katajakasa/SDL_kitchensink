#ifdef KIT_FAULT_INJECTION

#include <assert.h>
#include <string.h>

#include <SDL_atomic.h>
#include <SDL_mutex.h>

#include "kitchensink2/internal/kitfaultinject.h"

#define KIT_FAIL_POINT_MAX 32

typedef struct Kit_FailPointEntry {
    const char *name;
    int checks;             ///< Lifetime check counter (0 = never seen)
    bool armed;             ///< Whether an active arming window is set
    int arm_base;           ///< checks value recorded at arming time
    int first_failing_call; ///< 1-based index of the first failing check, relative to arm_base
    int arm_count;          ///< Window length; -1 = fail forever
    int error_code;         ///< Code delivered to Kit_TestFailPointCode on failure
} Kit_FailPointEntry;

static Kit_FailPointEntry fail_points[KIT_FAIL_POINT_MAX];
static int fail_point_count = 0;
static SDL_mutex *fail_point_mutex = NULL;
static SDL_SpinLock fail_point_mutex_create_lock = 0;

/**
 * @brief Lazily creates the shared registry mutex on first use.
 */
static SDL_mutex *Kit_GetFailPointMutex(void) {
    SDL_AtomicLock(&fail_point_mutex_create_lock);
    if(fail_point_mutex == NULL) {
        fail_point_mutex = SDL_CreateMutex();
    }
    SDL_mutex *m = fail_point_mutex;
    SDL_AtomicUnlock(&fail_point_mutex_create_lock);
    return m;
}

/**
 * @brief Finds a registry entry by name, creating it if unseen. Returns NULL if the
 * registry is full (asserted in debug builds).
 *
 * Caller must hold the registry mutex!
 */
static Kit_FailPointEntry *Kit_FindOrCreateFailPoint(const char *name) {
    for(int i = 0; i < fail_point_count; i++) {
        if(strcmp(fail_points[i].name, name) == 0) {
            return &fail_points[i];
        }
    }
    assert(fail_point_count < KIT_FAIL_POINT_MAX);
    if(fail_point_count >= KIT_FAIL_POINT_MAX) {
        return NULL; // Registry full; the point can never arm or fire.
    }
    Kit_FailPointEntry *entry = &fail_points[fail_point_count++];
    entry->name = name;
    entry->checks = 0;
    entry->armed = false;
    entry->arm_base = 0;
    entry->first_failing_call = 0;
    entry->arm_count = 0;
    entry->error_code = 0;
    return entry;
}

bool Kit_TestFailPointCode(const char *name, int *error_code) {
    SDL_mutex *mutex = Kit_GetFailPointMutex();
    SDL_LockMutex(mutex);

    Kit_FailPointEntry *entry = Kit_FindOrCreateFailPoint(name);
    if(entry == NULL) {
        SDL_UnlockMutex(mutex);
        return false;
    }
    entry->checks++;
    bool fail = false;
    if(entry->armed) {
        int n = entry->checks - entry->arm_base;
        if(n >= entry->first_failing_call &&
           (entry->arm_count == -1 || n < entry->first_failing_call + entry->arm_count)) {
            fail = true;
            if(error_code != NULL) {
                *error_code = entry->error_code;
            }
        }
    }

    SDL_UnlockMutex(mutex);
    return fail;
}

bool Kit_TestFailPoint(const char *name) {
    return Kit_TestFailPointCode(name, NULL);
}

void Kit_SetFailPoint(const char *name, int first_failing_call, int count, int error_code) {
    SDL_mutex *mutex = Kit_GetFailPointMutex();
    SDL_LockMutex(mutex);

    Kit_FailPointEntry *entry = Kit_FindOrCreateFailPoint(name);
    if(entry == NULL) {
        SDL_UnlockMutex(mutex);
        return;
    }
    entry->arm_base = entry->checks;
    entry->first_failing_call = first_failing_call;
    entry->arm_count = count;
    entry->error_code = error_code;
    entry->armed = true;

    SDL_UnlockMutex(mutex);
}

void Kit_ResetFailPoints(void) {
    SDL_mutex *mutex = Kit_GetFailPointMutex();
    SDL_LockMutex(mutex);

    fail_point_count = 0;
    memset(fail_points, 0, sizeof(fail_points));

    SDL_UnlockMutex(mutex);
}

int Kit_GetFailPointCount(const char *name) {
    SDL_mutex *mutex = Kit_GetFailPointMutex();
    SDL_LockMutex(mutex);

    int result = 0;
    for(int i = 0; i < fail_point_count; i++) {
        if(strcmp(fail_points[i].name, name) == 0) {
            result = fail_points[i].checks;
            break;
        }
    }

    SDL_UnlockMutex(mutex);
    return result;
}

#endif // KIT_FAULT_INJECTION
