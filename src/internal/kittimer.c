#include <SDL_atomic.h>

#include "kitchensink2/internal/kittimer.h"
#include "kitchensink2/internal/utils/kithelpers.h"
#include <stdlib.h>

typedef struct Kit_TimerValue {
    SDL_atomic_t count;
    bool initialized;
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
    }
}

void Kit_SetTimerBase(Kit_Timer *timer) {
    if(timer->writeable) {
        timer->ref->value = Kit_GetSystemTime();
        timer->ref->initialized = true;
    }
}

void Kit_AdjustTimerBase(Kit_Timer *timer, double adjust) {
    if(timer->writeable) {
        timer->ref->value = Kit_GetSystemTime() - adjust;
        timer->ref->initialized = true;
    }
}

void Kit_AddTimerBase(Kit_Timer *timer, double add) {
    if(timer->writeable) {
        timer->ref->value += add;
        timer->ref->initialized = true;
    }
}

double Kit_GetTimerElapsed(const Kit_Timer *timer) {
    return Kit_GetSystemTime() - timer->ref->value;
}

bool Kit_IsTimerPrimary(const Kit_Timer *timer) {
    return timer->writeable;
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
