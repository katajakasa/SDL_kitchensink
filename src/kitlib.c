#include <assert.h>
#ifdef USE_DYNAMIC_LIBASS
#include <SDL3/SDL_loadso.h>
#endif

#include "libavcodec/avcodec.h"
#include <libavformat/avformat.h>

#include "kitchensink3/internal/kitlibstate.h"
#include "kitchensink3/kitchensink.h"

static void _libass_msg_callback(int level, const char *fmt, va_list va, void *data) {
}

int Kit_InitASS(Kit_LibraryState *state) {
#ifdef USE_DYNAMIC_LIBASS
    state->ass_so_handle = SDL_LoadObject(DYNAMIC_LIBASS_NAME);
    if(state->ass_so_handle == NULL) {
        Kit_SetError("Unable to load ASS library");
        return 1;
    }
    if(load_libass(state->ass_so_handle) != 0) {
        Kit_SetError("Unable to load ASS library functions");
        SDL_UnloadObject(state->ass_so_handle);
        state->ass_so_handle = NULL;
        return 1;
    }
#endif
    state->libass_handle = ass_library_init();
    if(state->libass_handle == NULL) {
        Kit_SetError("Unable to initialize libass library");
#ifdef USE_DYNAMIC_LIBASS
        SDL_UnloadObject(state->ass_so_handle);
        state->ass_so_handle = NULL;
#endif
        return 1;
    }
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
    if(flags & KIT_INIT_NETWORK) {
        avformat_network_deinit();
    }
exit_0:
    return 1;
}

void Kit_Quit(void) {
    Kit_LibraryState *state = Kit_GetLibraryState();
    if(state->init_flags & KIT_INIT_NETWORK) {
        avformat_network_deinit();
    }
    if(state->init_flags & KIT_INIT_ASS) {
        Kit_CloseASS(state);
    }
    state->init_flags = 0;
}

void Kit_GetVersion(Kit_Version *version) {
    assert(version != NULL);
    version->major = KIT_VERSION_MAJOR;
    version->minor = KIT_VERSION_MINOR;
    version->patch = KIT_VERSION_PATCH;
}
