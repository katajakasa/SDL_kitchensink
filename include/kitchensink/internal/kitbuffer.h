#ifndef KITBUFFER_H
#define KITBUFFER_H

#include "kitchensink/kitconfig.h"

typedef struct Kit_Buffer {
    unsigned int read_p;
    unsigned int write_p;
    unsigned int size;
    void **data;
} Kit_Buffer;

KIT_LOCAL Kit_Buffer* Kit_CreateBuffer(unsigned int size);
KIT_LOCAL void Kit_DestroyBuffer(Kit_Buffer *buffer);

KIT_LOCAL void* Kit_ReadBuffer(Kit_Buffer *buffer);
KIT_LOCAL int Kit_WriteBuffer(Kit_Buffer *buffer, void *ptr);
KIT_LOCAL int Kit_IsBufferFull(const Kit_Buffer *buffer);

#endif // KITBUFFER_H
