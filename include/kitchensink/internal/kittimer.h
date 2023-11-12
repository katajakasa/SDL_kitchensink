#ifndef KITCLOCK_H
#define KITCLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include "kitchensink/kitlib.h"

typedef struct Kit_Timer Kit_Timer;

KIT_LOCAL Kit_Timer* Kit_CreateTimer();
KIT_LOCAL Kit_Timer* Kit_CreateSecondaryTimer(const Kit_Timer *src, bool writeable);

KIT_LOCAL void Kit_SetTimerBase(Kit_Timer *timer);
KIT_LOCAL void Kit_AdjustTimerBase(Kit_Timer *timer, double adjust);
KIT_LOCAL void Kit_AddTimerBase(Kit_Timer *timer, double add);
KIT_LOCAL double Kit_GetTimerElapsed(const Kit_Timer *timer);

KIT_LOCAL void Kit_CloseTimer(Kit_Timer **clock);


#endif // KITCLOCK_H