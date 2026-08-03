#ifndef KITTIMER_H
#define KITTIMER_H

/**
 * @brief Reference-counted playback clock. A primary (writeable) timer owns the shared base
 * value and seek serials; secondary timers share the same underlying value for read access
 * (and, if created writeable, can also modify it) and must each be closed independently.
 *
 * @file kittimer.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/kitlib.h"
#include <stdbool.h>

/**
 * @brief Opaque handle to a playback timer. See Kit_CreateTimer() / Kit_CreateSecondaryTimer().
 */
typedef struct Kit_Timer Kit_Timer;

/**
 * @brief Creates a new primary (writeable) timer with its own refcounted shared value,
 * uninitialized until Kit_InitTimerBase() or Kit_SetTimerBase() is called.
 *
 * @return New timer, or NULL on allocation failure (see Kit_GetError())
 */
KIT_LOCAL Kit_Timer *Kit_CreateTimer(void);
/**
 * @brief Creates a new timer handle sharing src's underlying value (increments its refcount).
 *
 * @param src Existing timer to share the value with
 * @param writeable Whether the new handle is allowed to modify the shared base/serial
 * @return New timer handle, or NULL on allocation failure (see Kit_GetError())
 */
KIT_LOCAL Kit_Timer *Kit_CreateSecondaryTimer(const Kit_Timer *src, bool writeable);

/**
 * @brief Sets the timer base to the current system time, but only if not already initialized.
 * No-op if the timer is not writeable.
 *
 * @param timer Timer to initialize
 */
KIT_LOCAL void Kit_InitTimerBase(Kit_Timer *timer);
/**
 * @brief Checks whether the shared timer value has been initialized. Locks the shared mutex.
 *
 * @param timer Timer to query
 * @return true if a base value has been set, false otherwise
 */
KIT_LOCAL bool Kit_IsTimerInitialized(const Kit_Timer *timer);
/**
 * @brief Marks the timer as uninitialized and unpaused. No-op if the timer is not writeable.
 *
 * @param timer Timer to reset
 */
KIT_LOCAL void Kit_ResetTimerBase(Kit_Timer *timer);
/**
 * @brief Unconditionally sets the timer base to the current system time and marks it
 * initialized. No-op if the timer is not writeable.
 *
 * @param timer Timer to set
 */
KIT_LOCAL void Kit_SetTimerBase(Kit_Timer *timer);
/**
 * @brief Rebases the timer so its elapsed value becomes `adjust`, and records `serial` as the
 * base serial (used by Kit_IsTimerSynced()). No-op if the timer is not writeable.
 *
 * @param timer Timer to adjust
 * @param adjust Elapsed time (seconds) the timer should report immediately after this call
 * @param serial Seek serial this base corresponds to
 */
KIT_LOCAL void Kit_AdjustTimerBase(Kit_Timer *timer, double adjust, unsigned int serial);
/**
 * @brief Adds `add` seconds to the timer base, shifting the reported elapsed time backward
 * relative to real time. No-op if the timer is not writeable.
 *
 * @param timer Timer to adjust
 * @param add Seconds to add to the internal base value
 */
KIT_LOCAL void Kit_AddTimerBase(Kit_Timer *timer, double add);
/**
 * @brief Pauses the timer if initialized and not already paused, freezing elapsed time.
 * No-op if the timer is not writeable.
 *
 * @param timer Timer to pause
 */
KIT_LOCAL void Kit_PauseTimer(Kit_Timer *timer);
/**
 * @brief Resumes a paused timer, folding the paused duration back into the base so elapsed
 * time continues from where it was paused. No-op if the timer is not writeable.
 *
 * @param timer Timer to resume
 */
KIT_LOCAL void Kit_ResumeTimer(Kit_Timer *timer);
/**
 * @brief Gets the elapsed time in seconds since the timer base, or 0.0 if the timer has never
 * been initialized. Available on both primary and secondary timers.
 *
 * @param timer Timer to query
 * @return Elapsed seconds, or 0.0 if uninitialized
 */
KIT_LOCAL double Kit_GetTimerElapsed(const Kit_Timer *timer);
/**
 * @brief Checks whether this handle is the writeable (primary or writeable-secondary) side.
 *
 * @param timer Timer to query
 * @return true if this handle can modify the shared base/serial, false otherwise
 */
KIT_LOCAL bool Kit_IsTimerPrimary(const Kit_Timer *timer);

/**
 * @brief Gets the current seek serial. Backed by an SDL atomic; safe to call from any thread.
 *
 * @param timer Timer to query
 * @return Current serial value
 */
KIT_LOCAL unsigned int Kit_GetTimerSerial(const Kit_Timer *timer);
/**
 * @brief Atomically increments the seek serial by one (e.g. on a new seek request).
 *
 * @param timer Timer to modify
 * @return New serial value after the increment
 */
KIT_LOCAL unsigned int Kit_IncreaseTimerSerial(Kit_Timer *timer);
/**
 * @brief Sets the base serial to `serial` if this handle is writeable; used to mark the timer
 * as caught up with a given seek. Backed by an SDL atomic. No-op if not writeable.
 *
 * @param timer Timer to modify
 * @param serial Serial value to record as the base serial
 */
KIT_LOCAL void Kit_SetTimerBaseSerial(Kit_Timer *timer, unsigned int serial);
/**
 * @brief Checks whether the base serial matches the current serial, i.e. the timer's base has
 * been updated for the latest seek. Backed by SDL atomics; safe to call from any thread.
 *
 * @param timer Timer to query
 * @return true if base_serial == serial, false otherwise
 */
KIT_LOCAL bool Kit_IsTimerSynced(const Kit_Timer *timer);

/**
 * @brief Releases this timer handle, decrementing the shared value's refcount and freeing the
 * shared value (and its mutex) once the last handle is closed.
 *
 * @param clock Pointer to the timer pointer; set to NULL on return. No-op if NULL or *clock is NULL.
 */
KIT_LOCAL void Kit_CloseTimer(Kit_Timer **clock);

#endif // KITTIMER_H
