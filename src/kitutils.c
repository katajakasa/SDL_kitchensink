#include <SDL.h>

#include "kitchensink/kitsource.h"
#include "kitchensink/kitutils.h"

const char *Kit_GetSDLAudioFormatString(unsigned int type) {
    switch(type) {
        case AUDIO_S8:
            return "AUDIO_S8";
        case AUDIO_U8:
            return "AUDIO_U8";
        case AUDIO_S16:
            return "AUDIO_S16";
        case AUDIO_U16:
            return "AUDIO_U16";
        case AUDIO_S32:
            return "AUDIO_S32";
        case AUDIO_F32:
            return "AUDIO_F32";
        default:
            return NULL;
    }
}

const char *Kit_GetSDLPixelFormatString(unsigned int type) {
    switch(type) {
        case SDL_PIXELFORMAT_UNKNOWN:
            return "SDL_PIXELFORMAT_UNKNOWN";
        case SDL_PIXELFORMAT_INDEX1LSB:
            return "SDL_PIXELFORMAT_INDEX1LSB";
        case SDL_PIXELFORMAT_INDEX1MSB:
            return "SDL_PIXELFORMAT_INDEX1MSB";
        case SDL_PIXELFORMAT_INDEX4LSB:
            return "SDL_PIXELFORMAT_INDEX4LSB";
        case SDL_PIXELFORMAT_INDEX4MSB:
            return "SDL_PIXELFORMAT_INDEX4MSB";
        case SDL_PIXELFORMAT_INDEX8:
            return "SDL_PIXELFORMAT_INDEX8";
        case SDL_PIXELFORMAT_RGB332:
            return "SDL_PIXELFORMAT_RGB332";
        case SDL_PIXELFORMAT_RGB444:
            return "SDL_PIXELFORMAT_RGB444";
        case SDL_PIXELFORMAT_RGB555:
            return "SDL_PIXELFORMAT_RGB555";
        case SDL_PIXELFORMAT_BGR555:
            return "SDL_PIXELFORMAT_BGR555";
        case SDL_PIXELFORMAT_ARGB4444:
            return "SDL_PIXELFORMAT_ARGB4444";
        case SDL_PIXELFORMAT_RGBA4444:
            return "SDL_PIXELFORMAT_RGBA4444";
        case SDL_PIXELFORMAT_ABGR4444:
            return "SDL_PIXELFORMAT_ABGR4444";
        case SDL_PIXELFORMAT_BGRA4444:
            return "SDL_PIXELFORMAT_BGRA4444";
        case SDL_PIXELFORMAT_ARGB1555:
            return "SDL_PIXELFORMAT_ARGB1555";
        case SDL_PIXELFORMAT_RGBA5551:
            return "SDL_PIXELFORMAT_RGBA5551";
        case SDL_PIXELFORMAT_ABGR1555:
            return "SDL_PIXELFORMAT_ABGR1555";
        case SDL_PIXELFORMAT_BGRA5551:
            return "SDL_PIXELFORMAT_BGRA5551";
        case SDL_PIXELFORMAT_RGB565:
            return "SDL_PIXELFORMAT_RGB565";
        case SDL_PIXELFORMAT_BGR565:
            return "SDL_PIXELFORMAT_BGR565";
        case SDL_PIXELFORMAT_RGB24:
            return "SDL_PIXELFORMAT_RGB24";
        case SDL_PIXELFORMAT_BGR24:
            return "SDL_PIXELFORMAT_BGR24";
        case SDL_PIXELFORMAT_RGB888:
            return "SDL_PIXELFORMAT_RGB888";
        case SDL_PIXELFORMAT_RGBX8888:
            return "SDL_PIXELFORMAT_RGBX8888";
        case SDL_PIXELFORMAT_BGR888:
            return "SDL_PIXELFORMAT_BGR888";
        case SDL_PIXELFORMAT_BGRX8888:
            return "SDL_PIXELFORMAT_BGRX8888";
        case SDL_PIXELFORMAT_ARGB8888:
            return "SDL_PIXELFORMAT_ARGB8888";
        case SDL_PIXELFORMAT_RGBA8888:
            return "SDL_PIXELFORMAT_RGBA8888";
        case SDL_PIXELFORMAT_ABGR8888:
            return "SDL_PIXELFORMAT_ABGR8888";
        case SDL_PIXELFORMAT_BGRA8888:
            return "SDL_PIXELFORMAT_BGRA8888";
        case SDL_PIXELFORMAT_ARGB2101010:
            return "SDL_PIXELFORMAT_ARGB2101010";
        case SDL_PIXELFORMAT_YV12:
            return "SDL_PIXELFORMAT_YV12";
        case SDL_PIXELFORMAT_IYUV:
            return "SDL_PIXELFORMAT_IYUV";
        case SDL_PIXELFORMAT_YUY2:
            return "SDL_PIXELFORMAT_YUY2";
        case SDL_PIXELFORMAT_UYVY:
            return "SDL_PIXELFORMAT_UYVY";
        case SDL_PIXELFORMAT_YVYU:
            return "SDL_PIXELFORMAT_YVYU";
        default:
            return NULL;
    }
}

const char *Kit_GetKitStreamTypeString(unsigned int type) {
    switch(type) {
        case KIT_STREAMTYPE_UNKNOWN:
            return "KIT_STREAMTYPE_UNKNOWN";
        case KIT_STREAMTYPE_VIDEO:
            return "KIT_STREAMTYPE_VIDEO";
        case KIT_STREAMTYPE_AUDIO:
            return "KIT_STREAMTYPE_AUDIO";
        case KIT_STREAMTYPE_DATA:
            return "KIT_STREAMTYPE_DATA";
        case KIT_STREAMTYPE_SUBTITLE:
            return "KIT_STREAMTYPE_SUBTITLE";
        case KIT_STREAMTYPE_ATTACHMENT:
            return "KIT_STREAMTYPE_ATTACHMENT";
        default:
            return NULL;
    }
}
