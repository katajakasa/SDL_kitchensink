#ifndef KITLIBSTATE_H
#define KITLIBSTATE_H

#include "kitchensink/internal/libass.h"
#include "kitchensink/kitconfig.h"

typedef struct Kit_LibraryState {
    unsigned int init_flags;
    unsigned int thread_count;
    unsigned int font_hinting;
    unsigned int video_packet_buffer_size;
    unsigned int audio_packet_buffer_size;
    unsigned int subtitle_packet_buffer_size;
    unsigned int video_frame_buffer_size;
    unsigned int audio_frame_buffer_size;
    unsigned int subtitle_frame_buffer_size;
    unsigned int video_early_threshold;
    unsigned int video_late_threshold;
    unsigned int audio_early_threshold;
    unsigned int audio_late_threshold;
    ASS_Library *libass_handle;
    void *ass_so_handle;
} Kit_LibraryState;

KIT_LOCAL Kit_LibraryState *Kit_GetLibraryState();

#endif // KITLIBSTATE_H
