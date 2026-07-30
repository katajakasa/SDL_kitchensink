#ifndef KITLIBSTATE_H
#define KITLIBSTATE_H

/**
 * @brief Process-wide singleton holding SDL_kitchensink's init flags, buffer/threshold hints,
 * and libass handles.
 *
 * @file kitlibstate.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/internal/libass.h"
#include "kitchensink2/kitconfig.h"

/**
 * @brief Global library state: init flags, all Kit_HintType-backed values, and libass handles.
 * There is exactly one static instance, accessed via Kit_GetLibraryState().
 */
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
    unsigned int demuxer_read_attempts;
    unsigned int demuxer_read_retry_delay;
    ASS_Library *libass_handle;
    void *ass_so_handle;
} Kit_LibraryState;

/**
 * @brief Gets a pointer to the single process-wide library state instance.
 *
 * @return Pointer to the static Kit_LibraryState instance (never NULL)
 */
KIT_LOCAL Kit_LibraryState *Kit_GetLibraryState();

#endif // KITLIBSTATE_H
