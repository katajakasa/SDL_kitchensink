#ifndef KITCHENSINK_H
#define KITCHENSINK_H

#include <SDL2/SDL.h>
#include "kitchensink/kiterror.h"
#include "kitchensink/kitsource.h"
#include "kitchensink/kitplayer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIT_VERSION(x) \
    (x)->major = KIT_VERSION_MAJOR; \
    (x)->minor = KIT_VERSION_MINOR; \
    (x)->patch = KIT_VERSION_PATCH

typedef struct Kit_Version {
    Uint8 major;
    Uint8 minor;
    Uint8 patch;
} Kit_Version;

enum {
    KIT_INIT_FORMATS = 0x1,
    KIT_INIT_NETWORK = 0x2,
};

int Kit_Init();
void Kit_Quit();

void Kit_GetVersion(Kit_Version *version);

#ifdef __cplusplus
}
#endif

#endif // KITCHENSINK_H
