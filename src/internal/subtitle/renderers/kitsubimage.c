#include "kitchensink/internal/subtitle/renderers/kitsubimage.h"

int filler() { return 0; }

/*
void _HandleBitmapSubtitle(Kit_SubtitlePacket** spackets, int *n, Kit_Player *player, double pts, AVSubtitle *sub, AVSubtitleRect *rect) {
    if(rect->nb_colors == 256) {
        // Paletted image based subtitles. Convert and set palette.
        SDL_Surface *s = SDL_CreateRGBSurfaceFrom(
            rect->data[0],
            rect->w, rect->h, 8,
            rect->linesize[0],
            0, 0, 0, 0);

        SDL_SetPaletteColors(s->format->palette, (SDL_Color*)rect->data[1], 0, 256);

        Uint32 rmask, gmask, bmask, amask;
        #if SDL_BYTEORDER == SDL_BIG_ENDIAN
            rmask = 0xff000000;
            gmask = 0x00ff0000;
            bmask = 0x0000ff00;
            amask = 0x000000ff;
        #else
            rmask = 0x000000ff;
            gmask = 0x0000ff00;
            bmask = 0x00ff0000;
            amask = 0xff000000;
        #endif
        SDL_Surface *tmp = SDL_CreateRGBSurface(
            0, rect->w, rect->h, 32,
            rmask, gmask, bmask, amask);
        SDL_BlitSurface(s, NULL, tmp, NULL);
        SDL_FreeSurface(s);

        SDL_Rect *dst_rect = malloc(sizeof(SDL_Rect));
        dst_rect->x = rect->x;
        dst_rect->y = rect->y;
        dst_rect->w = rect->w;
        dst_rect->h = rect->h;

        double start = pts + (sub->start_display_time / 1000.0f);
        double end = -1;
        if(sub->end_display_time < UINT_MAX) {
            end = pts + (sub->end_display_time / 1000.0f);
        }

        spackets[(*n)++] = _CreateSubtitlePacket(start, end, dst_rect, tmp);
    }
}

*/