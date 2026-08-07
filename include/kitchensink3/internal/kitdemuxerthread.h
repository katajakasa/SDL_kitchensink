#ifndef KITDEMUXERTHREAD_H
#define KITDEMUXERTHREAD_H

/**
 * @brief Background thread that repeatedly runs a Kit_Demuxer until stopped or EOF, and applies a pending seek at
 * the start of its next run. On EOF, it sends an EOF sentinel packet to every active stream buffer.
 *
 * @file kitdemuxerthread.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink3/internal/kitbufferindex.h"
#include "kitchensink3/internal/kitdemuxer.h"
#include "kitchensink3/internal/kitpacketbuffer.h"
#include "kitchensink3/kitconfig.h"
#include <SDL3/SDL_thread.h>
#include <stdbool.h>

/**
 * @brief Demuxer thread state: the demuxer it drives, SDL thread handle, run flag and pending seek request.
 */
typedef struct Kit_DemuxerThread {
    Kit_Demuxer *demuxer;
    SDL_Thread *thread;
    SDL_AtomicInt run;
    bool seek;           ///< Seek request flag; may only be set while the thread is not running
    int64_t seek_target; ///< Seek target position; may only be set while the thread is not running
} Kit_DemuxerThread;

/**
 * @brief Creates a demuxer thread bound to a demuxer, but does not start it.
 *
 * @param demuxer Demuxer this thread will drive; not owned by the thread.
 * @return New demuxer thread (not started), or NULL on allocation failure.
 */
KIT_LOCAL Kit_DemuxerThread *Kit_CreateDemuxerThread(Kit_Demuxer *demuxer);

/**
 * @brief Stops, waits for, and frees a demuxer thread.
 *
 * @param demuxer Pointer to the demuxer thread pointer; set to NULL after closing. No-op if NULL or already-NULL.
 */
KIT_LOCAL void Kit_CloseDemuxerThread(Kit_DemuxerThread **demuxer);

/**
 * @brief Queues a seek to be performed as soon as the demuxer thread is (re)started.
 *
 * May only be called while the thread is stopped (asserts demuxer_thread->thread == NULL). The actual seek
 * runs on the demuxer thread itself, at the very start of its loop after the next Kit_StartDemuxerThread().
 *
 * @param demuxer_thread Thread to queue the seek on; must currently be stopped.
 * @param seek_target Target position, in AV_TIME_BASE units, forwarded to Kit_DemuxerSeek().
 */
KIT_LOCAL void Kit_SeekDemuxerThread(Kit_DemuxerThread *demuxer_thread, int64_t seek_target);

/**
 * @brief Gets the underlying demuxer's packet buffer for a given stream type.
 *
 * @param demuxer_thread Thread whose demuxer to query; must not be NULL.
 * @param buffer_index Stream type to look up.
 * @return The packet buffer for that stream type (may be NULL if that stream type is unused).
 */
KIT_LOCAL Kit_PacketBuffer *
Kit_GetDemuxerThreadPacketBuffer(const Kit_DemuxerThread *demuxer_thread, Kit_BufferIndex buffer_index);

/**
 * @brief Starts the demuxer thread's SDL thread. No-op if already running or @p demuxer_thread is NULL.
 *
 * @param demuxer_thread Thread to start.
 */
KIT_LOCAL void Kit_StartDemuxerThread(Kit_DemuxerThread *demuxer_thread);

/**
 * @brief Clears the run flag, asking the demuxer thread to exit at its next loop check.
 *
 * This only clears the flag; it does not wake up a thread blocked writing into a full packet buffer. If the
 * thread may be blocked, call Kit_AbortDemuxer() as well, or Kit_WaitDemuxerThread() can deadlock.
 *
 * @param demuxer_thread Thread to stop; no-op if NULL or not running.
 */
KIT_LOCAL void Kit_StopDemuxerThread(Kit_DemuxerThread *demuxer_thread);

/**
 * @brief Blocks until the demuxer thread's SDL thread has exited, then clears the thread handle.
 *
 * Can deadlock if the thread is blocked on a full packet buffer and Kit_StopDemuxerThread() alone was
 * called; make sure Kit_AbortDemuxer() has been called first if that's possible.
 *
 * @param demuxer_thread Thread to wait for; no-op if NULL or not running.
 */
KIT_LOCAL void Kit_WaitDemuxerThread(Kit_DemuxerThread *demuxer_thread);

/**
 * @brief Checks whether the demuxer thread's run flag is still set.
 *
 * @param demuxer_thread Thread to query.
 * @return true if the run flag is set (thread is running or about to exit on next check), false otherwise.
 */
KIT_LOCAL bool Kit_IsDemuxerThreadAlive(Kit_DemuxerThread *demuxer_thread);

#endif // KITDEMUXERTHREAD_H
