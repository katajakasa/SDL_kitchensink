#include "kitchensink/internal/kitbuffer.h"

#include <stdlib.h>
#include <assert.h>

Kit_Buffer* Kit_CreateBuffer(unsigned int size) {
    Kit_Buffer *b = calloc(1, sizeof(Kit_Buffer));
    b->size = size;
    b->data = calloc(size, sizeof(void*));
    return b;
}

void Kit_DestroyBuffer(Kit_Buffer *buffer) {
    assert(buffer != NULL);
    free(buffer->data);
    free(buffer);
}

void* Kit_ReadBuffer(Kit_Buffer *buffer) {
    assert(buffer != NULL);
    if(buffer->read_p < buffer->write_p) {
        void *out = buffer->data[buffer->read_p % buffer->size];
        buffer->data[buffer->read_p % buffer->size] = NULL;
        buffer->read_p++;
        if(buffer->read_p >= buffer->size) {
            buffer->read_p = buffer->read_p % buffer->size;
            buffer->write_p = buffer->write_p % buffer->size;
        }
        return out;
    }
    return NULL;
}

int Kit_WriteBuffer(Kit_Buffer *buffer, void *ptr) {
    assert(buffer != NULL);
    assert(ptr != NULL);

    if(!Kit_IsBufferFull(buffer)) {
        buffer->data[buffer->write_p % buffer->size] = ptr;
        buffer->write_p++;
        return 0;
    }
    return 1;
}

int Kit_IsBufferFull(const Kit_Buffer *buffer) {
    return (buffer->write_p - buffer->read_p >= buffer->size);
}
