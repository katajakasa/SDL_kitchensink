#ifndef KITSUBTITLE_H
#define KITSUBTITLE_H

#include <libavformat/avformat.h>

#include "kitchensink/kitconfig.h"
#include "kitchensink/kitplayer.h"

#define KIT_SBUFFERSIZE 512

typedef struct Kit_SubtitlePacket {
    double pts_start;
    double pts_end;
    SDL_Rect *rect;
    SDL_Surface *surface;
    SDL_Texture *texture;
} Kit_SubtitlePacket;

KIT_LOCAL Kit_SubtitlePacket* _CreateSubtitlePacket(double pts_start, double pts_end, SDL_Rect *rect, SDL_Surface *surface);
KIT_LOCAL void _FreeSubtitlePacket(void *ptr);
KIT_LOCAL void _HandleBitmapSubtitle(Kit_SubtitlePacket** spackets, int *n, Kit_Player *player, double pts, AVSubtitle *sub, AVSubtitleRect *rect);
KIT_LOCAL void _HandleSubtitlePacket(Kit_Player *player, AVPacket *packet);

#endif // KITSUBTITLE_H
