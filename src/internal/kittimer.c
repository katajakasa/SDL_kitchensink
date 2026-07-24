#include <SDL_atomic.h>

#include "kitchensink2/internal/kittimer.h"
#include "kitchensink2/internal/utils/kithelpers.h"
#include <stdlib.h>

typedef struct Kit_TimerValue {
    SDL_atomic_t count;       ///< Reference count
    SDL_atomic_t serial;      ///< Current seek serial; bumped on every seek request
    SDL_atomic_t base_serial; ///< Seek serial for which the timer base was last set
    bool initialized;
    bool paused;
    double pause_start;
    double value;
} Kit_TimerValue;

struct Kit_Timer {
    bool writeable;
    Kit_TimerValue *ref;
};

Kit_Timer *Kit_CreateTimer() {
    Kit_Timer *timer;
    Kit_TimerValue *value;

    if((timer = calloc(1, sizeof(Kit_Timer))) == NULL) {
        goto exit_0;
    }
    if((value = calloc(1, sizeof(Kit_TimerValue))) == NULL) {
        goto exit_1;
    }

    SDL_AtomicSet(&value->count, 1);
    SDL_AtomicSet(&value->serial, 0);
    SDL_AtomicSet(&value->base_serial, 0);
    value->value = 0;
    value->initialized = false;
    timer->ref = value;
    timer->writeable = true;
    return timer;

exit_1:
    free(timer);
exit_0:
    return NULL;
}

Kit_Timer *Kit_CreateSecondaryTimer(const Kit_Timer *src, bool writeable) {
    Kit_Timer *timer;
    if((timer = calloc(1, sizeof(Kit_Timer))) == NULL) {
        return NULL;
    }
    timer->ref = src->ref;
    SDL_AtomicAdd(&timer->ref->count, 1);
    timer->writeable = writeable;
    return timer;
}

void Kit_InitTimerBase(Kit_Timer *timer) {
    if(timer->writeable && !timer->ref->initialized) {
        timer->ref->value = Kit_GetSystemTime();
        timer->ref->initialized = true;
    }
}

bool Kit_IsTimerInitialized(const Kit_Timer *timer) {
    return timer->ref->initialized;
}

void Kit_ResetTimerBase(Kit_Timer *timer) {
    if(timer->writeable) {
        timer->ref->initialized = false;
        timer->ref->paused = false;
    }
}

void Kit_SetTimerBase(Kit_Timer *timer) {
    if(timer->writeable) {
        timer->ref->value = Kit_GetSystemTime();
        timer->ref->pause_start = timer->ref->value;
        timer->ref->initialized = true;
    }
}

void Kit_AdjustTimerBase(Kit_Timer *timer, double adjust, unsigned int serial) {
    if(timer->writeable) {
        const double now = Kit_GetSystemTime();
        timer->ref->value = now - adjust;
        timer->ref->pause_start = now;
        timer->ref->initialized = true;
        SDL_AtomicSet(&timer->ref->base_serial, (int)serial);
    }
}

void Kit_AddTimerBase(Kit_Timer *timer, double add) {
    if(timer->writeable) {
        timer->ref->value += add;
        timer->ref->initialized = true;
    }
}

void Kit_PauseTimer(Kit_Timer *timer) {
    if(timer->writeable && timer->ref->initialized && !timer->ref->paused) {
        timer->ref->pause_start = Kit_GetSystemTime();
        timer->ref->paused = true;
    }
}

void Kit_ResumeTimer(Kit_Timer *timer) {
    if(timer->writeable && timer->ref->paused) {
        timer->ref->value += Kit_GetSystemTime() - timer->ref->pause_start;
        timer->ref->paused = false;
    }
}

double Kit_GetTimerElapsed(const Kit_Timer *timer) {
    if(timer->ref->paused)
        return timer->ref->pause_start - timer->ref->value;
    return Kit_GetSystemTime() - timer->ref->value;
}

bool Kit_IsTimerPrimary(const Kit_Timer *timer) {
    return timer->writeable;
}

unsigned int Kit_GetTimerSerial(const Kit_Timer *timer) {
    return (unsigned int)SDL_AtomicGet(&timer->ref->serial);
}

unsigned int Kit_IncreaseTimerSerial(Kit_Timer *timer) {
    return (unsigned int)SDL_AtomicAdd(&timer->ref->serial, 1) + 1;
}

void Kit_SetTimerBaseSerial(Kit_Timer *timer, unsigned int serial) {
    if(timer->writeable) {
        SDL_AtomicSet(&timer->ref->base_serial, (int)serial);
    }
}

bool Kit_IsTimerSynced(const Kit_Timer *timer) {
    return SDL_AtomicGet(&timer->ref->base_serial) == SDL_AtomicGet(&timer->ref->serial);
}

void Kit_CloseTimer(Kit_Timer **ref) {
    if(!ref || !*ref)
        return;
    Kit_Timer *timer = *ref;
    if(SDL_AtomicAdd(&timer->ref->count, -1) == 1) {
        free(timer->ref);
    }
    free(timer);
    *ref = NULL;
}
