#ifndef KITLIB_H
#define KITLIB_H

#include "kitchensink/kitconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { // These should match libass
    KIT_FONT_HINTING_NONE = 0,
    KIT_FONT_HINTING_LIGHT,
    KIT_FONT_HINTING_NORMAL,
    KIT_FONT_HINTING_NATIVE,
    KIT_FONT_HINTING_COUNT
};

typedef struct Kit_Version {
    unsigned char major;
    unsigned char minor;
    unsigned char patch;
} Kit_Version;

typedef enum Kit_HintType {
    KIT_HINT_FONT_HINTING,
    KIT_HINT_THREAD_COUNT,
    KIT_HINT_VIDEO_BUFFER_FRAMES,
    KIT_HINT_AUDIO_BUFFER_FRAMES,
    KIT_HINT_SUBTITLE_BUFFER_FRAMES
} Kit_HintType;

enum {
    KIT_INIT_NETWORK = 0x1,
    KIT_INIT_ASS = 0x2
};

KIT_API int Kit_Init(unsigned int flags);
KIT_API void Kit_Quit();
KIT_API void Kit_SetHint(Kit_HintType type, int value);
KIT_API int Kit_GetHint(Kit_HintType type);
KIT_API void Kit_GetVersion(Kit_Version *version);

#ifdef __cplusplus
}
#endif

#endif // KITLIB_H
