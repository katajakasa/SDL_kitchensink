#ifndef KITVIDEO_H
#define KITVIDEO_H

#include <libavformat/avformat.h>

#include "kitchensink/kitconfig.h"
#include "kitchensink/kitplayer.h"

#define KIT_VBUFFERSIZE 3

typedef struct Kit_VideoPacket {
    double pts;
    AVFrame *frame;
} Kit_VideoPacket;

KIT_LOCAL Kit_VideoPacket* _CreateVideoPacket(AVFrame *frame, double pts);
KIT_LOCAL void _FreeVideoPacket(void *ptr);
KIT_LOCAL void _FindPixelFormat(enum AVPixelFormat fmt, unsigned int *out_fmt);
KIT_LOCAL enum AVPixelFormat _FindAVPixelFormat(unsigned int fmt);
KIT_LOCAL void _HandleVideoPacket(Kit_Player *player, AVPacket *packet);

#endif // KITVIDEO_H
