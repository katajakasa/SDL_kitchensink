#ifndef KITFRAMESTREAM_H
#define KITFRAMESTREAM_H

/**
 * @brief Thread-safe, fixed-capacity circular buffer of opaque objects (packets/frames),
 * used to pass data between the demuxer/decoder threads and the reader threads.
 *
 * @file kitpacketbuffer.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink3/kitconfig.h"
#include <stdbool.h>

typedef void *(*buf_obj_alloc)();
typedef void (*buf_obj_unref)(void *obj);
typedef void (*buf_obj_free)(void **obj);
typedef void (*buf_obj_move)(void *dst, void *src);
typedef void (*buf_obj_ref)(void *dst, void *src);

/**
 * @brief Opaque thread-safe circular buffer of pre-allocated objects. See Kit_CreatePacketBuffer().
 */
typedef struct Kit_PacketBuffer Kit_PacketBuffer;

/**
 * @brief Allocates a packet buffer and pre-allocates `capacity` slot objects via alloc_cb.
 *
 * @param capacity Number of slots to pre-allocate (must be > 0)
 * @param alloc_cb Callback used to allocate each slot object
 * @param unref_cb Callback used to release the contents of a slot object (keeps it allocated)
 * @param free_cb Callback used to fully free a slot object
 * @param move_cb Callback used to move an object's contents from src into dst
 * @param ref_cb Callback used to take a reference from src into dst (may be NULL if the buffer is
 * never read via Kit_BeginPacketBufferRead())
 *
 * @return New packet buffer, or NULL on allocation failure (see Kit_GetError())
 */
KIT_LOCAL Kit_PacketBuffer *Kit_CreatePacketBuffer(
    size_t capacity,
    buf_obj_alloc alloc_cb,
    buf_obj_unref unref_cb,
    buf_obj_free free_cb,
    buf_obj_move move_cb,
    buf_obj_ref ref_cb
);
/**
 * @brief Frees all slot objects and destroys the buffer. All reader/writer threads must have
 * already stopped before calling this.
 *
 * @param buffer Pointer to the buffer pointer; set to NULL on return. No-op if NULL or *buffer is NULL.
 */
KIT_LOCAL void Kit_FreePacketBuffer(Kit_PacketBuffer **buffer);

/**
 * @brief Gets the total slot capacity of the buffer.
 *
 * @param buffer Buffer to query
 * @return Capacity in slots
 */
KIT_LOCAL size_t Kit_GetPacketBufferCapacity(const Kit_PacketBuffer *buffer);
/**
 * @brief Gets the number of currently filled slots. Thread-safe (locks the buffer mutex).
 *
 * @param buffer Buffer to query
 * @return Number of filled slots
 */
KIT_LOCAL size_t Kit_GetPacketBufferLength(const Kit_PacketBuffer *buffer);

/**
 * @brief Marks the buffer as aborted and wakes all readers/writers waiting on it, causing their
 * calls to fail immediately. Subsequent read/write calls also fail until Kit_FlushPacketBuffer()
 * clears the aborted flag.
 *
 * @param buffer Buffer to abort; no-op if NULL
 */
KIT_LOCAL void Kit_AbortPacketBuffer(Kit_PacketBuffer *buffer);
/**
 * @brief Releases all slot contents, resets the buffer to empty, and clears the aborted flag.
 * Wakes up any writers waiting for free space.
 *
 * @param buffer Buffer to flush; no-op if NULL
 */
KIT_LOCAL void Kit_FlushPacketBuffer(Kit_PacketBuffer *buffer);
/**
 * @brief Moves src into the next free slot, blocking while the buffer is full until space frees
 * up or the buffer is aborted.
 *
 * @param buffer Buffer to write to
 * @param src Object whose contents are moved into the buffer via the move callback
 * @return true on success, false if the buffer is or becomes aborted
 */
KIT_LOCAL bool Kit_WritePacketBuffer(Kit_PacketBuffer *buffer, void *src);
/**
 * @brief Moves the oldest slot's contents into dst, blocking up to timeout ms if the buffer is
 * empty.
 *
 * @param buffer Buffer to read from
 * @param dst Destination receiving the moved-out contents via the move callback
 * @param timeout Max time to wait for data, in milliseconds; <= 0 fails immediately if empty
 * @return true on success, false on timeout, abort, or empty buffer with no wait
 */
KIT_LOCAL bool Kit_ReadPacketBuffer(Kit_PacketBuffer *buffer, void *dst, int timeout);

/**
 * @brief Begins a reference-based read of the oldest slot, blocking up to timeout ms if the
 * buffer is empty. On success, the buffer mutex remains held until Kit_FinishPacketBufferRead()
 * or Kit_CancelPacketBufferRead() is called; the slot is not yet advanced.
 *
 * @param buffer Buffer to read from
 * @param dst Destination receiving a reference to the slot's contents via the ref callback
 * @param timeout Max time to wait for data, in milliseconds; <= 0 fails immediately if empty
 * @return true on success (mutex held), false on timeout, abort, or empty buffer with no wait
 */
KIT_LOCAL bool Kit_BeginPacketBufferRead(Kit_PacketBuffer *buffer, void *dst, int timeout);
/**
 * @brief Completes a Kit_BeginPacketBufferRead(): releases the slot's contents, advances the
 * read position, releases the buffer mutex, and wakes a waiting writer.
 *
 * @param buffer Buffer previously passed to a successful Kit_BeginPacketBufferRead() call
 */
KIT_LOCAL void Kit_FinishPacketBufferRead(Kit_PacketBuffer *buffer);
/**
 * @brief Cancels a Kit_BeginPacketBufferRead() without consuming the slot: releases the buffer
 * mutex, leaving the slot in place for a future read.
 *
 * @param buffer Buffer previously passed to a successful Kit_BeginPacketBufferRead() call
 */
KIT_LOCAL void Kit_CancelPacketBufferRead(Kit_PacketBuffer *buffer);

#endif // KITFRAMESTREAM_H
