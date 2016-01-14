#include "kitchensink/kitchensink.h"
#include <libavformat/avformat.h>
#include <assert.h>

static unsigned int _init_flags = 0;

int Kit_Init(unsigned int flags) {
    if(_init_flags != 0) {
        Kit_SetError("Kitchensink is already initialized.");
        return 1;
    }
    if(flags & KIT_INIT_NETWORK) {
        avformat_network_init();
    }
    if(flags & KIT_INIT_FORMATS) {
        av_register_all();
    }
    _init_flags = flags;
    return 0;
}

void Kit_Quit() {
    if(_init_flags & KIT_INIT_NETWORK) {
        avformat_network_deinit();
    }
    _init_flags = 0;
}

void Kit_GetVersion(Kit_Version *version) {
    assert(version != NULL);
    version->major = KIT_VERSION_MAJOR;
    version->minor = KIT_VERSION_MINOR;
    version->patch = KIT_VERSION_PATCH;
}
