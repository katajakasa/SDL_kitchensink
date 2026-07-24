#ifndef KITTIMER_H
#define KITTIMER_H

#include "kitchensink2/kitlib.h"
#include <stdbool.h>

typedef struct Kit_Timer Kit_Timer;

KIT_LOCAL Kit_Timer *Kit_CreateTimer();
KIT_LOCAL Kit_Timer *Kit_CreateSecondaryTimer(const Kit_Timer *src, bool writeable);

KIT_LOCAL void Kit_InitTimerBase(Kit_Timer *timer);
KIT_LOCAL bool Kit_IsTimerInitialized(const Kit_Timer *timer);
KIT_LOCAL void Kit_ResetTimerBase(Kit_Timer *timer);
KIT_LOCAL void Kit_SetTimerBase(Kit_Timer *timer);
KIT_LOCAL void Kit_AdjustTimerBase(Kit_Timer *timer, double adjust, unsigned int serial);
KIT_LOCAL void Kit_AddTimerBase(Kit_Timer *timer, double add);
KIT_LOCAL void Kit_PauseTimer(Kit_Timer *timer);
KIT_LOCAL void Kit_ResumeTimer(Kit_Timer *timer);
KIT_LOCAL double Kit_GetTimerElapsed(const Kit_Timer *timer);
KIT_LOCAL bool Kit_IsTimerPrimary(const Kit_Timer *timer);

KIT_LOCAL unsigned int Kit_GetTimerSerial(const Kit_Timer *timer);
KIT_LOCAL unsigned int Kit_IncreaseTimerSerial(Kit_Timer *timer);
KIT_LOCAL void Kit_SetTimerBaseSerial(Kit_Timer *timer, unsigned int serial);
KIT_LOCAL bool Kit_IsTimerSynced(const Kit_Timer *timer);

KIT_LOCAL void Kit_CloseTimer(Kit_Timer **clock);

#endif // KITTIMER_H
