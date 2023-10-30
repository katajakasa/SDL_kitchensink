#include <assert.h>
#include <SDL_mutex.h>

#include "kitchensink/internal/kitpacketbuffer.h"
#include "kitchensink/internal/utils/kitlog.h"
#include "kitchensink/kiterror.h"

struct Kit_PacketBuffer {
    void **packets;
    SDL_mutex *mutex;
    SDL_cond *can_read;
    SDL_cond *can_write;
    size_t head;
    size_t tail;
    size_t capacity;
    bool full;
    buf_obj_unref unref_cb;
    buf_obj_free free_cb;
    buf_obj_move move_cb;
};


Kit_PacketBuffer* Kit_CreatePacketBuffer(
    size_t capacity,
    buf_obj_alloc alloc_cb,
    buf_obj_unref unref_cb,
    buf_obj_free free_cb,
    buf_obj_move move_cb
) {
    assert(capacity> 0);
    Kit_PacketBuffer *buffer = NULL;
    SDL_mutex *mutex = NULL;
    SDL_cond *can_write = NULL;
    SDL_cond *can_read = NULL;
    void **packets;

    if((can_write = SDL_CreateCond()) == NULL) {
        Kit_SetError("Unable to allocate writer conditional variable: %s", SDL_GetError());
        goto error_0;
    }
    if((can_read = SDL_CreateCond()) == NULL) {
        Kit_SetError("Unable to allocate reader conditional variable: %s", SDL_GetError());
        goto error_1;
    }
    if((mutex = SDL_CreateMutex()) == NULL) {
        Kit_SetError("Unable to allocate mutex: %s", SDL_GetError());
        goto error_2;
    }
    if((packets = calloc(capacity, sizeof(void*))) == NULL) {
        Kit_SetError("Unable to allocate packet buffer");
        goto error_3;
    }
    for(size_t i = 0; i < capacity; i++) {
        if((packets[i] = alloc_cb()) == NULL) {
            Kit_SetError("Unable to allocate av_packet");
            goto error_4;
        }
    }
    if((buffer = malloc(sizeof(Kit_PacketBuffer))) == NULL) {
        Kit_SetError("Unable to allocate packet stream");
        goto error_4;
    }

    buffer->packets = packets;
    buffer->can_write = can_write;
    buffer->can_read = can_read;
    buffer->mutex = mutex;
    buffer->capacity = capacity;
    buffer->head = 0;
    buffer->tail = 0;
    buffer->full = false;
    buffer->unref_cb = unref_cb;
    buffer->free_cb = free_cb;
    buffer->move_cb = move_cb;
    return buffer;

error_4:
    for(size_t i = 0; i < capacity; i++) {
        if(packets[i] != NULL) {
            free_cb((void **)&packets[i]);
        }
    }
    free(packets);
error_3:
    SDL_DestroyMutex(mutex);
error_2:
    SDL_DestroyCond(can_read);
error_1:
    SDL_DestroyCond(can_write);
error_0:
    return NULL;
}

void Kit_FreePacketBuffer(Kit_PacketBuffer **ref) {
    if (!ref || !*ref)
        return;

    Kit_PacketBuffer *buffer = *ref;
    SDL_DestroyCond(buffer->can_read);
    SDL_DestroyCond(buffer->can_write);
    SDL_DestroyMutex(buffer->mutex);
    for(size_t i = 0; i < buffer->capacity; i++) {
        buffer->free_cb((void **)&buffer->packets[i]);
    }
    free(buffer);
    *ref = NULL;
}

bool Kit_IsPacketBufferFull(const Kit_PacketBuffer *buffer) {
    assert(buffer);
    return buffer->full;
}

bool Kit_IsPacketBufferEmpty(const Kit_PacketBuffer *buffer) {
    assert(buffer);
    return (!buffer->full && (buffer->head == buffer->tail));
}

size_t Kit_GetPacketBufferCapacity(const Kit_PacketBuffer *buffer) {
    assert(buffer);
    return buffer->capacity;
}

size_t Kit_GetPacketBufferLength(const Kit_PacketBuffer *buffer) {
    assert(buffer);
    if (buffer->full)
        return buffer->capacity;
    return (buffer->head >= buffer->tail)
        ? buffer->head - buffer->tail
        : buffer->capacity + buffer->head - buffer->tail;
}

void Kit_FlushPacketBuffer(Kit_PacketBuffer *buffer) {
    assert(buffer);
    if(SDL_LockMutex(buffer->mutex) == 0) {
        for(size_t i = 0; i < buffer->capacity; i++) {
            buffer->unref_cb(buffer->packets[i]);
        }
        buffer->head = 0;
        buffer->tail = 0;
        SDL_UnlockMutex(buffer->mutex);
    }
}

static void advance_read(Kit_PacketBuffer *buffer) {
    assert(buffer);
    buffer->full = false;
    if (++(buffer->tail) == buffer->capacity)
        buffer->tail = 0;
}

static void advance_write(Kit_PacketBuffer *buffer) {
    assert(buffer);
    if(buffer->full)
        if (++(buffer->tail) == buffer->capacity)
            buffer->tail = 0;
    if (++(buffer->head) == buffer->capacity)
        buffer->head = 0;
    buffer->full = (buffer->head == buffer->tail);
}

bool Kit_WritePacketBuffer(Kit_PacketBuffer *buffer, void *src) {
    assert(buffer);
    assert(src);
    if(SDL_LockMutex(buffer->mutex) < 0)
        goto error_0;
    if(Kit_IsPacketBufferFull(buffer))
        SDL_CondWait(buffer->can_write, buffer->mutex);
    if(Kit_IsPacketBufferFull(buffer))
        goto error_1;
    buffer->move_cb(buffer->packets[buffer->head], src);
    advance_write(buffer);
    //LOG("WRITE -- HEAD = %lld, TAIL = %lld, USED = %lld/%lld\n", buffer->head, buffer->tail, Kit_GetPacketBufferLength(buffer), buffer->capacity);
    SDL_UnlockMutex(buffer->mutex);
    SDL_CondSignal(buffer->can_read);
    return true;

error_1:
    SDL_UnlockMutex(buffer->mutex);
error_0:
    return false;
}

bool Kit_ReadPacketBuffer(Kit_PacketBuffer *buffer, void *dst, int timeout) {
    assert(buffer);
    if(SDL_LockMutex(buffer->mutex) < 0)
        goto error_0;
    if(Kit_IsPacketBufferEmpty(buffer)) {
        if(timeout <= 0)
            goto error_1;
        if(SDL_CondWaitTimeout(buffer->can_read, buffer->mutex, timeout) == SDL_MUTEX_TIMEDOUT)
            goto error_1;
    }
    if(Kit_IsPacketBufferEmpty(buffer))
        goto error_1;
    buffer->move_cb(dst, buffer->packets[buffer->tail]);
    advance_read(buffer);
    //LOG("READ -- HEAD = %lld, TAIL = %lld, USED = %lld/%lld\n", buffer->head, buffer->tail, Kit_GetPacketBufferLength(buffer), buffer->capacity);
    SDL_UnlockMutex(buffer->mutex);
    SDL_CondSignal(buffer->can_write);
    return true;

error_1:
    SDL_UnlockMutex(buffer->mutex);
error_0:
    return false;
}
