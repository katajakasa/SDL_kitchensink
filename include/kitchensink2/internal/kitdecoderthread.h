#ifndef KITDECODERTHREAD_H
#define KITDECODERTHREAD_H

/**
 * @brief Background thread that pulls packets from a Kit_PacketBuffer, feeds them into a Kit_Decoder, and drives
 * decoding until stopped or end-of-stream is reached, re-basing the sync clock as needed after seeks.
 *
 * @file kitdecoderthread.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/internal/kitdecoder.h"
#include "kitchensink2/internal/kitpacketbuffer.h"
#include "kitchensink2/kitconfig.h"
#include <SDL_thread.h>
#include <stdbool.h>

/**
 * @brief Decoder thread state: the input packet buffer, target decoder, SDL thread handle and run flag.
 */
typedef struct Kit_DecoderThread {
    Kit_PacketBuffer *input;  ///< Packet buffer this thread reads from (owned elsewhere, e.g. the demuxer).
    Kit_Decoder *decoder;     ///< Decoder this thread drives.
    SDL_Thread *thread;       ///< Underlying SDL thread handle; NULL while not running.
    AVPacket *scratch_packet; ///< Reusable packet used to read from the input buffer.
    SDL_atomic_t run;         ///< Run flag; 0 requests/marks stop.
} Kit_DecoderThread;

/**
 * @brief Creates a decoder thread bound to an input packet buffer and a decoder, but does not start it.
 *
 * @param input Packet buffer to read input packets from.
 * @param decoder Decoder to drive; not closed or owned by the thread.
 * @return New decoder thread (not started), or NULL on allocation failure.
 */
KIT_LOCAL Kit_DecoderThread *Kit_CreateDecoderThread(Kit_PacketBuffer *input, Kit_Decoder *decoder);

/**
 * @brief Starts the decoder thread's SDL thread. No-op if already running or @p decoder_thread is NULL.
 *
 * @param decoder_thread Thread to start.
 * @param name Name given to the underlying SDL thread (for debugging).
 */
KIT_LOCAL void Kit_StartDecoderThread(Kit_DecoderThread *decoder_thread, const char *name);

/**
 * @brief Clears the run flag, asking the decoder thread to exit at its next loop check.
 *
 * This only clears the flag; it does not wake up a thread blocked reading from an empty/full packet buffer.
 * If the thread may be blocked, call Kit_AbortDecoder() (on its decoder) as well, or Kit_WaitDecoderThread()
 * can deadlock.
 *
 * @param decoder_thread Thread to stop; no-op if NULL or not running.
 */
KIT_LOCAL void Kit_StopDecoderThread(Kit_DecoderThread *decoder_thread);

/**
 * @brief Blocks until the decoder thread's SDL thread has exited, then clears the thread handle.
 *
 * Can deadlock if the thread is blocked on a full/empty buffer and Kit_StopDecoderThread() alone was called;
 * make sure the associated decoder/buffer has been aborted first if that's possible.
 *
 * @param decoder_thread Thread to wait for; no-op if NULL or not running.
 */
KIT_LOCAL void Kit_WaitDecoderThread(Kit_DecoderThread *decoder_thread);

/**
 * @brief Stops, waits for, and frees a decoder thread (including its scratch packet).
 *
 * @param ref Pointer to the decoder thread pointer; set to NULL after closing. No-op if NULL or already-NULL.
 */
KIT_LOCAL void Kit_CloseDecoderThread(Kit_DecoderThread **ref);

/**
 * @brief Checks whether the decoder thread's run flag is still set.
 *
 * @param decoder_thread Thread to query.
 * @return true if the run flag is set (thread is running or about to exit on next check), false otherwise.
 */
KIT_LOCAL bool Kit_IsDecoderThreadAlive(Kit_DecoderThread *decoder_thread);

#endif // KITDECODERTHREAD_H
