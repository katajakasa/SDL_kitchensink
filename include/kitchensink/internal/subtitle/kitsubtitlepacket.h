#ifndef KITSUBTITLEPACKET_H
#define KITSUBTITLEPACKET_H

#include <SDL_surface.h>
#include <stdbool.h>

#include "kitchensink/kitconfig.h"

typedef struct Kit_SubtitlePacket {
    double pts_start;
    double pts_end;
    int x;
    int y;
    bool clear;
    SDL_Surface *surface;
} Kit_SubtitlePacket;

KIT_LOCAL Kit_SubtitlePacket *Kit_CreateSubtitlePacket();
KIT_LOCAL void Kit_FreeSubtitlePacket(Kit_SubtitlePacket **packet);
KIT_LOCAL void Kit_SetSubtitlePacketData(
    Kit_SubtitlePacket *packet,
    bool clear,
    double pts_start,
    double pts_end,
    int pos_x,
    int pos_y,
    SDL_Surface *surface
);
KIT_LOCAL void Kit_MoveSubtitlePacketRefs(Kit_SubtitlePacket *dst, Kit_SubtitlePacket *src);
KIT_LOCAL void Kit_DelSubtitlePacketRefs(Kit_SubtitlePacket *packet);

// Not implemented
KIT_LOCAL void Kit_CreateSubtitlePacketRef(Kit_SubtitlePacket *dst, Kit_SubtitlePacket *src);

#endif // KITSUBTITLEPACKET_H
