#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_mutex.h>

#include "kitchensink3/internal/kitfaultinject.h"
#include "kitchensink3/internal/kittimer.h"
#include "kitchensink3/internal/utils/kitalloc.h"
#include "kitchensink3/internal/utils/kithelpers.h"
#include "kitchensink3/kiterror.h"
#include <stdlib.h>

typedef struct Kit_TimerValue {
    SDL_AtomicInt count;       ///< Reference count
    SDL_AtomicInt serial;      ///< Current seek serial; bumped on every seek request
    SDL_AtomicInt base_serial; ///< Seek serial for which the timer base was last set
    SDL_Mutex *lock;           ///< Guards the non-atomic fields below (shared by multiple threads)
    bool initialized;
    bool paused;
    double pause_start;
    double value;
} Kit_TimerValue;

struct Kit_Timer {
    bool writeable;
    Kit_TimerValue *ref;
};

Kit_Timer *Kit_CreateTimer(void) {
    Kit_Timer *timer;
    Kit_TimerValue *value;

    if((timer = Kit_Calloc(1, sizeof(Kit_Timer))) == NULL) {
        Kit_SetError("Unable to allocate timer");
        goto exit_0;
    }
    if((value = Kit_Calloc(1, sizeof(Kit_TimerValue))) == NULL) {
        Kit_SetError("Unable to allocate timer value");
        goto exit_1;
    }
    if((value->lock = KIT_FAULT_WRAP_PTR("sdl_mutex", SDL_CreateMutex())) == NULL) {
        Kit_SetError("Unable to allocate timer mutex: %s", SDL_GetError());
        goto exit_2;
    }

    SDL_SetAtomicInt(&value->count, 1);
    SDL_SetAtomicInt(&value->serial, 0);
    SDL_SetAtomicInt(&value->base_serial, 0);
    value->value = 0;
    value->initialized = false;
    timer->ref = value;
    timer->writeable = true;
    return timer;

exit_2:
    free(value);
exit_1:
    free(timer);
exit_0:
    return NULL;
}

Kit_Timer *Kit_CreateSecondaryTimer(const Kit_Timer *src, bool writeable) {
    Kit_Timer *timer;
    if((timer = Kit_Calloc(1, sizeof(Kit_Timer))) == NULL) {
        Kit_SetError("Unable to allocate secondary timer");
        return NULL;
    }
    timer->ref = src->ref;
    SDL_AddAtomicInt(&timer->ref->count, 1);
    timer->writeable = writeable;
    return timer;
}

void Kit_InitTimerBase(Kit_Timer *timer) {
    if(!timer->writeable)
        return;
    const double now = Kit_GetSystemTime();
    SDL_LockMutex(timer->ref->lock);
    if(!timer->ref->initialized) {
        timer->ref->value = now;
        timer->ref->initialized = true;
    }
    SDL_UnlockMutex(timer->ref->lock);
}

bool Kit_IsTimerInitialized(const Kit_Timer *timer) {
    SDL_LockMutex(timer->ref->lock);
    const bool initialized = timer->ref->initialized;
    SDL_UnlockMutex(timer->ref->lock);
    return initialized;
}

void Kit_ResetTimerBase(Kit_Timer *timer) {
    if(!timer->writeable)
        return;
    SDL_LockMutex(timer->ref->lock);
    timer->ref->initialized = false;
    timer->ref->paused = false;
    SDL_UnlockMutex(timer->ref->lock);
}

void Kit_SetTimerBase(Kit_Timer *timer) {
    if(!timer->writeable)
        return;
    const double now = Kit_GetSystemTime();
    SDL_LockMutex(timer->ref->lock);
    timer->ref->value = now;
    timer->ref->pause_start = now;
    timer->ref->initialized = true;
    SDL_UnlockMutex(timer->ref->lock);
}

void Kit_AdjustTimerBase(Kit_Timer *timer, double adjust, unsigned int serial) {
    if(!timer->writeable)
        return;
    const double now = Kit_GetSystemTime();
    SDL_LockMutex(timer->ref->lock);
    timer->ref->value = now - adjust;
    timer->ref->pause_start = now;
    timer->ref->initialized = true;
    SDL_SetAtomicInt(&timer->ref->base_serial, (int)serial);
    SDL_UnlockMutex(timer->ref->lock);
}

void Kit_AddTimerBase(Kit_Timer *timer, double add) {
    if(!timer->writeable)
        return;
    SDL_LockMutex(timer->ref->lock);
    timer->ref->value += add;
    timer->ref->initialized = true;
    SDL_UnlockMutex(timer->ref->lock);
}

void Kit_PauseTimer(Kit_Timer *timer) {
    if(!timer->writeable)
        return;
    const double now = Kit_GetSystemTime();
    SDL_LockMutex(timer->ref->lock);
    if(timer->ref->initialized && !timer->ref->paused) {
        timer->ref->pause_start = now;
        timer->ref->paused = true;
    }
    SDL_UnlockMutex(timer->ref->lock);
}

void Kit_ResumeTimer(Kit_Timer *timer) {
    if(!timer->writeable)
        return;
    const double now = Kit_GetSystemTime();
    SDL_LockMutex(timer->ref->lock);
    if(timer->ref->paused) {
        timer->ref->value += now - timer->ref->pause_start;
        timer->ref->paused = false;
    }
    SDL_UnlockMutex(timer->ref->lock);
}

double Kit_GetTimerElapsed(const Kit_Timer *timer) {
    const double now = Kit_GetSystemTime();
    SDL_LockMutex(timer->ref->lock);
    double elapsed = 0.0;
    if(timer->ref->initialized)
        elapsed = timer->ref->paused ? timer->ref->pause_start - timer->ref->value : now - timer->ref->value;
    SDL_UnlockMutex(timer->ref->lock);
    return elapsed;
}

bool Kit_IsTimerPrimary(const Kit_Timer *timer) {
    return timer->writeable;
}

unsigned int Kit_GetTimerSerial(const Kit_Timer *timer) {
    return (unsigned int)SDL_GetAtomicInt(&timer->ref->serial);
}

unsigned int Kit_IncreaseTimerSerial(Kit_Timer *timer) {
    return (unsigned int)SDL_AddAtomicInt(&timer->ref->serial, 1) + 1;
}

void Kit_SetTimerBaseSerial(Kit_Timer *timer, unsigned int serial) {
    if(timer->writeable) {
        SDL_SetAtomicInt(&timer->ref->base_serial, (int)serial);
    }
}

bool Kit_IsTimerSynced(const Kit_Timer *timer) {
    return SDL_GetAtomicInt(&timer->ref->base_serial) == SDL_GetAtomicInt(&timer->ref->serial);
}

void Kit_CloseTimer(Kit_Timer **ref) {
    if(!ref || !*ref)
        return;
    Kit_Timer *timer = *ref;
    if(SDL_AddAtomicInt(&timer->ref->count, -1) == 1) {
        SDL_DestroyMutex(timer->ref->lock);
        free(timer->ref);
    }
    free(timer);
    *ref = NULL;
}
