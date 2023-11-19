#include <SDL_video.h>
#include <libavcodec/avcodec.h>

#include "kitchensink/internal/video/kitvideoutils.h"

static enum AVPixelFormat supported_list[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUYV422,
    AV_PIX_FMT_UYVY422,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV21,
    AV_PIX_FMT_RGB24,
    AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGB555,
    AV_PIX_FMT_BGR555,
    AV_PIX_FMT_RGB565,
    AV_PIX_FMT_BGR565,
    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_RGBA,
    AV_PIX_FMT_NONE};

enum AVPixelFormat Kit_FindBestAVPixelFormat(enum AVPixelFormat fmt)
{
    return avcodec_find_best_pix_fmt_of_list(supported_list, fmt, 1, NULL);
}

unsigned int Kit_FindSDLPixelFormat(enum AVPixelFormat fmt) {
    switch(fmt) {
        case AV_PIX_FMT_YUV420P:
            return SDL_PIXELFORMAT_YV12;
        case AV_PIX_FMT_YUYV422:
            return SDL_PIXELFORMAT_YUY2;
        case AV_PIX_FMT_UYVY422:
            return SDL_PIXELFORMAT_UYVY;
        case AV_PIX_FMT_NV12:
            return SDL_PIXELFORMAT_NV12;
        case AV_PIX_FMT_NV21:
            return SDL_PIXELFORMAT_NV21;
        default:
            return SDL_PIXELFORMAT_RGBA32;
    }
}

enum AVPixelFormat Kit_FindAVPixelFormat(unsigned int fmt)
{
    switch(fmt) {
        case SDL_PIXELFORMAT_YV12:
            return AV_PIX_FMT_YUV420P;
        case SDL_PIXELFORMAT_YUY2:
            return AV_PIX_FMT_YUYV422;
        case SDL_PIXELFORMAT_UYVY:
            return AV_PIX_FMT_UYVY422;
        case SDL_PIXELFORMAT_NV12:
            return AV_PIX_FMT_NV12;
        case SDL_PIXELFORMAT_NV21:
            return AV_PIX_FMT_NV21;
        case SDL_PIXELFORMAT_ARGB32:
            return AV_PIX_FMT_ARGB;
        case SDL_PIXELFORMAT_RGBA32:
            return AV_PIX_FMT_RGBA;
        case SDL_PIXELFORMAT_BGR24:
            return AV_PIX_FMT_BGR24;
        case SDL_PIXELFORMAT_RGB24:
            return AV_PIX_FMT_RGB24;
        case SDL_PIXELFORMAT_RGB555:
            return AV_PIX_FMT_RGB555;
        case SDL_PIXELFORMAT_BGR555:
            return AV_PIX_FMT_BGR555;
        case SDL_PIXELFORMAT_RGB565:
            return AV_PIX_FMT_RGB565;
        case SDL_PIXELFORMAT_BGR565:
            return AV_PIX_FMT_BGR565;
        default:
            return AV_PIX_FMT_NONE;
    }
}