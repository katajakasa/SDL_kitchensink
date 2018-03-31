#ifndef KITSUBTITLEPACKET_H
#define KITSUBTITLEPACKET_H

#include <SDL2/SDL_surface.h>

#include "kitchensink/kitconfig.h"

typedef enum {
    KIT_SUBTITLE_PACKET_ADD,
    KIT_SUBTITLE_PACKET_CLEAR
} Kit_SubtitlePacketType;

typedef struct Kit_SubtitlePacket {
    Kit_SubtitlePacketType type;
    double pts_start;
    double pts_end;
    int x;
    int y;
    SDL_Surface *surface;
} Kit_SubtitlePacket;

KIT_LOCAL Kit_SubtitlePacket* Kit_CreateSubtitlePacket(
    Kit_SubtitlePacketType type, double pts_start, double pts_end, int pos_x, int pos_y, SDL_Surface *surface);
KIT_LOCAL void Kit_FreeSubtitlePacket(Kit_SubtitlePacket *packet);

#endif // KITSUBTITLEPACKET_H
