#include "kitchensink/internal/kittimer.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include <stdlib.h>

typedef struct Kit_TimerValue {
    int count;
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

    value->count = 1;
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
    timer->ref->count++;
    timer->writeable = writeable;
    return timer;
}

void Kit_SetTimerBase(Kit_Timer *timer) {
    if(timer->writeable) {
        timer->ref->value = Kit_GetSystemTime();
    }
}

void Kit_AdjustTimerBase(Kit_Timer *timer, double adjust) {
    if(timer->writeable) {
        timer->ref->value = Kit_GetSystemTime() - adjust;
    }
}

void Kit_AddTimerBase(Kit_Timer *timer, double add) {
    if(timer->writeable) {
        timer->ref->value += add;
    }
}

double Kit_GetTimerElapsed(const Kit_Timer *timer) {
    return Kit_GetSystemTime() - timer->ref->value;
}

void Kit_CloseTimer(Kit_Timer **ref) {
    if(!ref || !*ref)
        return;
    Kit_Timer *timer = *ref;
    if(--timer->ref->count == 0) {
        free(timer->ref);
    }
    free(timer);
    *ref = NULL;
}
