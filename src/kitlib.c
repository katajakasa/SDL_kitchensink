#include <assert.h>

#include <libavformat/avformat.h>

#include "kitchensink/kitchensink.h"
#include "kitchensink/internal/kitlibstate.h"

#ifdef USE_ASS
#include <ass/ass.h>

static void _libass_msg_callback(int level, const char *fmt, va_list va, void *data) {}

int Kit_InitASS(Kit_LibraryState *state) {
    state->libass_handle = ass_library_init();
    ass_set_message_cb(state->libass_handle, _libass_msg_callback, NULL);
    return 0;
}

void Kit_CloseASS(Kit_LibraryState *state) {
    ass_library_done(state->libass_handle);
    state->libass_handle = NULL;
}

#else

int Kit_InitASS(Kit_LibraryState *state) { return 1; }
void Kit_CloseASS(Kit_LibraryState *state) {}

#endif


int Kit_Init(unsigned int flags) {
    Kit_LibraryState *state = Kit_GetLibraryState();

    if(state->init_flags != 0) {
        Kit_SetError("Kitchensink is already initialized.");
        return 1;
    }
    av_register_all();

    if(flags & KIT_INIT_NETWORK) {
        avformat_network_init();
        state->init_flags |= KIT_INIT_NETWORK;
    }
    if(flags & KIT_INIT_ASS) {
        if(Kit_InitASS(state) == 0) {
            state->init_flags |= KIT_INIT_ASS;
        }
    }
    return 0;
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

void Kit_GetVersion(Kit_Version *version) {
    assert(version != NULL);
    version->major = KIT_VERSION_MAJOR;
    version->minor = KIT_VERSION_MINOR;
    version->patch = KIT_VERSION_PATCH;
}
