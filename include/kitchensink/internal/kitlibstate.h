#ifndef KITLIBSTATE_H
#define KITLIBSTATE_H

#include "kitchensink/internal/libass.h"
#include "kitchensink/kitconfig.h"

typedef struct Kit_LibraryState {
    unsigned int init_flags;
    unsigned int thread_count;
    unsigned int font_hinting;
    unsigned int video_buf_frames;
    unsigned int audio_buf_frames;
    unsigned int subtitle_buf_frames;
    unsigned int video_buf_packets;
    unsigned int audio_buf_packets;
    unsigned int subtitle_buf_packets;
    int analyze_duration;
    int probe_size;
    ASS_Library *libass_handle;
    void *ass_so_handle;
} Kit_LibraryState;

KIT_LOCAL Kit_LibraryState* Kit_GetLibraryState();

#endif // KITLIBSTATE_H
