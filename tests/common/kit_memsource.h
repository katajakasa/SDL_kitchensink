/**
 * Shared in-memory custom-IO source helpers: load a fixture file fully into
 * memory with load_file(), then serve it to Kit_CreateSourceFromCustom() via
 * the MemFile struct and its mem_read()/mem_seek() callback pair.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */
#ifndef KIT_MEMSOURCE_H
#define KIT_MEMSOURCE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avio.h>
#include <libavutil/error.h>

#include "kitchensink3/kitchensink.h"

/** @brief In-memory file, used as userdata for the mem_read()/mem_seek() callback pair. */
typedef struct MemFile {
    unsigned char *data;
    int64_t size;
    int64_t pos;
} MemFile;

/** @brief Reads the whole fixture at `path` into a malloc'd buffer; 0 on success, -1 on failure. */
static inline int load_file(const char *path, unsigned char **out_data, int64_t *out_size) {
    unsigned char *buf = NULL;
    FILE *fp = fopen(path, "rb");
    if(fp == NULL)
        goto error;
    if(fseek(fp, 0, SEEK_END) != 0)
        goto error;
    const long size = ftell(fp);
    if(size < 0 || fseek(fp, 0, SEEK_SET) != 0)
        goto error;
    if((buf = malloc(size > 0 ? (size_t)size : 1)) == NULL)
        goto error;
    if(fread(buf, 1, (size_t)size, fp) != (size_t)size)
        goto error;
    fclose(fp);
    *out_data = buf;
    *out_size = size;
    return 0;

error:
    free(buf);
    if(fp != NULL)
        fclose(fp);
    return -1;
}

/** @brief Kit_ReadCallback over a MemFile; returns AVERROR_EOF once exhausted. */
static inline int mem_read(void *userdata, uint8_t *buf, int size) {
    MemFile *mem = userdata;
    if(mem->pos >= mem->size) {
        return AVERROR_EOF;
    }
    int64_t remaining = mem->size - mem->pos;
    int to_copy = (int)(remaining < size ? remaining : size);
    memcpy(buf, mem->data + mem->pos, (size_t)to_copy);
    mem->pos += to_copy;
    return to_copy;
}

/** @brief Kit_SeekCallback over a MemFile; supports SEEK_SET/CUR/END and the AVSEEK_SIZE query. */
static inline int64_t mem_seek(void *userdata, int64_t offset, int whence) {
    MemFile *mem = userdata;
    if(whence & AVSEEK_SIZE) {
        return mem->size;
    }
    int64_t new_pos;
    switch(whence & ~AVSEEK_FORCE) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = mem->pos + offset;
            break;
        case SEEK_END:
            new_pos = mem->size + offset;
            break;
        default:
            return -1;
    }
    if(new_pos < 0 || new_pos > mem->size) {
        return -1;
    }
    mem->pos = new_pos;
    return mem->pos;
}

#endif // KIT_MEMSOURCE_H
