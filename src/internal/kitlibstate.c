#include <limits.h>
#include "kitchensink/internal/kitlibstate.h"

static Kit_LibraryState _library_state = {
    .init_flags = 0,
    .thread_count = 0,
    .font_hinting = 0,
    .video_packet_buffer_size = 16,
    .audio_packet_buffer_size = 64,
    .subtitle_packet_buffer_size = 64,
    .video_frame_buffer_size = 2,
    .audio_frame_buffer_size = 16,
    .subtitle_frame_buffer_size = 64,
    .libass_handle = NULL,
    .ass_so_handle = NULL,
};

Kit_LibraryState* Kit_GetLibraryState() {
    return &_library_state;
}
