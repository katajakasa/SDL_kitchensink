#ifndef KITSUBTITLEPACKET_H
#define KITSUBTITLEPACKET_H

#include <SDL2/SDL_Surface.h>

typedef struct Kit_SubtitlePacket {
    double pts_start;
    double pts_end;
    int x;
    int y;
    SDL_Surface *surface;
} Kit_SubtitlePacket;

Kit_SubtitlePacket* Kit_CreateSubtitlePacket(
    double pts_start, double pts_end, int pos_x, int pos_y, SDL_Surface *surface);
void Kit_FreeSubtitlePacket(Kit_SubtitlePacket *packet);

#endif // KITSUBTITLEPACKET_H
