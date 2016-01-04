#include "kitchensink/kitchensink.h"
#include <libavformat/avformat.h>

static Uint32 _init_flags = 0;

int Kit_Init(Uint32 flags) {
    if(flags & KIT_INIT_NETWORK)
       avformat_network_init();
    if(flags & KIT_INIT_FORMATS)
	   av_register_all();
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
    version->major = KIT_VERSION_MAJOR;
    version->minor = KIT_VERSION_MINOR;
    version->patch = KIT_VERSION_PATCH;
}
