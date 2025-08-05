#include <assert.h>
#ifdef USE_DYNAMIC_LIBASS
#include <SDL_loadso.h>
#endif

#include "libavcodec/avcodec.h"
#include <libavformat/avformat.h>

#include "kitchensink2/internal/kitlibstate.h"
#include "kitchensink2/internal/utils/kithelpers.h"
#include "kitchensink2/kitchensink.h"

static void _libass_msg_callback(int level, const char *fmt, va_list va, void *data) {
}

int Kit_InitASS(Kit_LibraryState *state) {
#ifdef USE_DYNAMIC_LIBASS
    state->ass_so_handle = SDL_LoadObject(DYNAMIC_LIBASS_NAME);
    if(state->ass_so_handle == NULL) {
        Kit_SetError("Unable to load ASS library");
        return 1;
    }
    load_libass(state->ass_so_handle);
#endif
    state->libass_handle = ass_library_init();
    ass_set_message_cb(state->libass_handle, _libass_msg_callback, NULL);
    return 0;
}

void Kit_CloseASS(Kit_LibraryState *state) {
    ass_library_done(state->libass_handle);
    state->libass_handle = NULL;
#ifdef USE_DYNAMIC_LIBASS
    SDL_UnloadObject(state->ass_so_handle);
    state->ass_so_handle = NULL;
#endif
}

int Kit_Init(unsigned int flags) {
    Kit_LibraryState *state = Kit_GetLibraryState();

    if(state->init_flags != 0) {
        Kit_SetError("SDL_kitchensink is already initialized");
        goto exit_0;
    }
    if(flags & KIT_INIT_NETWORK) {
        avformat_network_init();
    }
    if(flags & KIT_INIT_ASS) {
        if(Kit_InitASS(state) != 0) {
            Kit_SetError("Failed to initialize libass");
            goto exit_1;
        }
    }

    // Disable ffmpeg logging.
    av_log_set_level(AV_LOG_QUIET);

    state->init_flags = flags;
    return 0;

exit_1:
    avformat_network_deinit();
exit_0:
    return 1;
}

void Kit_Quit() {
    Kit_LibraryState *state = Kit_GetLibraryState();
    if(state->init_flags & KIT_INIT_NETWORK) {
        avformat_network_deinit();
    }
    if(state->init_flags & KIT_INIT_ASS) {
        Kit_CloseASS(state);
    }
    state->init_flags = 0;
}

void Kit_SetHint(Kit_HintType type, int value) {
    Kit_LibraryState *state = Kit_GetLibraryState();
    switch(type) {
        case KIT_HINT_THREAD_COUNT:
            state->thread_count = Kit_max(value, 0);
            break;
        case KIT_HINT_FONT_HINTING:
            state->font_hinting = Kit_max(Kit_min(value, KIT_FONT_HINTING_COUNT - 1), 0);
            break;
        case KIT_HINT_VIDEO_BUFFER_PACKETS:
            state->video_packet_buffer_size = Kit_max(value, 1);
            break;
        case KIT_HINT_AUDIO_BUFFER_PACKETS:
            state->audio_packet_buffer_size = Kit_max(value, 1);
            break;
        case KIT_HINT_SUBTITLE_BUFFER_PACKETS:
            state->subtitle_packet_buffer_size = Kit_max(value, 1);
            break;
        case KIT_HINT_VIDEO_BUFFER_FRAMES:
            state->video_frame_buffer_size = Kit_max(value, 1);
            break;
        case KIT_HINT_AUDIO_BUFFER_FRAMES:
            state->audio_frame_buffer_size = Kit_max(value, 1);
            break;
        case KIT_HINT_SUBTITLE_BUFFER_FRAMES:
            state->subtitle_frame_buffer_size = Kit_max(value, 1);
            break;
        case KIT_HINT_AUDIO_EARLY_THRESHOLD:
            state->audio_early_threshold = Kit_max(value, 0);
            break;
        case KIT_HINT_AUDIO_LATE_THRESHOLD:
            state->audio_late_threshold = Kit_max(value, 0);
            break;
        case KIT_HINT_VIDEO_EARLY_THRESHOLD:
            state->video_early_threshold = Kit_max(value, 0);
            break;
        case KIT_HINT_VIDEO_LATE_THRESHOLD:
            state->video_late_threshold = Kit_max(value, 0);
            break;
    }
}

int Kit_GetHint(Kit_HintType type) {
    const Kit_LibraryState *state = Kit_GetLibraryState();
    switch(type) {
        case KIT_HINT_THREAD_COUNT:
            return state->thread_count;
        case KIT_HINT_FONT_HINTING:
            return state->font_hinting;
        case KIT_HINT_VIDEO_BUFFER_PACKETS:
            return state->video_packet_buffer_size;
        case KIT_HINT_AUDIO_BUFFER_PACKETS:
            return state->audio_packet_buffer_size;
        case KIT_HINT_SUBTITLE_BUFFER_PACKETS:
            return state->subtitle_packet_buffer_size;
        case KIT_HINT_VIDEO_BUFFER_FRAMES:
            return state->video_frame_buffer_size;
        case KIT_HINT_AUDIO_BUFFER_FRAMES:
            return state->audio_frame_buffer_size;
        case KIT_HINT_SUBTITLE_BUFFER_FRAMES:
            return state->subtitle_frame_buffer_size;
        case KIT_HINT_AUDIO_EARLY_THRESHOLD:
            return state->audio_early_threshold;
        case KIT_HINT_AUDIO_LATE_THRESHOLD:
            return state->audio_late_threshold;
        case KIT_HINT_VIDEO_EARLY_THRESHOLD:
            return state->video_early_threshold;
        case KIT_HINT_VIDEO_LATE_THRESHOLD:
            return state->video_late_threshold;
        default:
            return 0;
    }
}

void Kit_GetVersion(Kit_Version *version) {
    assert(version != NULL);
    version->major = KIT_VERSION_MAJOR;
    version->minor = KIT_VERSION_MINOR;
    version->patch = KIT_VERSION_PATCH;
}
