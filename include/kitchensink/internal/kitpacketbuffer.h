#ifndef KITFRAMESTREAM_H
#define KITFRAMESTREAM_H

#include <stdbool.h>
#include "kitchensink/kitconfig.h"

typedef void* (*buf_obj_alloc)();
typedef void (*buf_obj_unref)(void *obj);
typedef void (*buf_obj_free)(void **obj);
typedef void (*buf_obj_move)(void *dst, void *src);
typedef void (*buf_obj_ref)(void *dst, void *src);

typedef struct Kit_PacketBuffer Kit_PacketBuffer;

KIT_LOCAL Kit_PacketBuffer* Kit_CreatePacketBuffer(
    size_t capacity,
    buf_obj_alloc alloc_cb,
    buf_obj_unref unref_cb,
    buf_obj_free free_cb,
    buf_obj_move move_cb,
    buf_obj_ref ref_cb
);
KIT_LOCAL void Kit_FreePacketBuffer(Kit_PacketBuffer **buffer);

KIT_LOCAL bool Kit_IsPacketBufferFull(const Kit_PacketBuffer *buffer);
KIT_LOCAL bool Kit_IsPacketBufferEmpty(const Kit_PacketBuffer *buffer);
KIT_LOCAL size_t Kit_GetPacketBufferCapacity(const Kit_PacketBuffer *buffer);
KIT_LOCAL size_t Kit_GetPacketBufferLength(const Kit_PacketBuffer *buffer);

KIT_LOCAL void Kit_SignalPacketBuffer(Kit_PacketBuffer *buffer);
KIT_LOCAL void Kit_FlushPacketBuffer(Kit_PacketBuffer *buffer);
KIT_LOCAL bool Kit_WritePacketBuffer(Kit_PacketBuffer *buffer, void *src);
KIT_LOCAL bool Kit_ReadPacketBuffer(Kit_PacketBuffer *buffer, void *dst, int timeout);

KIT_LOCAL bool Kit_BeginPacketBufferRead(Kit_PacketBuffer *buffer, void *dst, int timeout);
KIT_LOCAL void Kit_FinishPacketBufferRead(Kit_PacketBuffer *buffer);
KIT_LOCAL void Kit_CancelPacketBufferRead(Kit_PacketBuffer *buffer);

#endif // KITFRAMESTREAM_H
