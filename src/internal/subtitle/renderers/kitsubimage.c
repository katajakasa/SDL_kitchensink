#include <assert.h>
#include <stdlib.h>

#include <SDL2/SDL_surface.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/utils/kitlog.h"
#include "kitchensink/internal/subtitle/kitatlas.h"
#include "kitchensink/internal/subtitle/kitsubtitlepacket.h"
#include "kitchensink/internal/subtitle/renderers/kitsubimage.h"


static void _ProcessSubImage(SDL_Surface *surface, const AVSubtitleRect *rect, int min_x, int min_y) {
    SDL_Surface *src = SDL_CreateRGBSurfaceWithFormatFrom(
        rect->data[0], rect->w, rect->h, 8, rect->linesize[0], SDL_PIXELFORMAT_INDEX8);
    SDL_SetPaletteColors(src->format->palette, (SDL_Color*)rect->data[1], 0, 256);

    SDL_Rect dst_rect;
    dst_rect.x = rect->x - min_x;
    dst_rect.y = rect->y - min_y;

    SDL_BlitSurface(src, NULL, surface, &dst_rect);
    SDL_FreeSurface(src);    
}

static Kit_SubtitlePacket* ren_render_image_cb(Kit_SubtitleRenderer *ren, void *src, double start_pts, double end_pts) {
    assert(ren != NULL);
    assert(src != NULL);

    AVSubtitle *sub = src;
    SDL_Surface *surface = NULL;
    int x0 = INT_MAX, y0 = INT_MAX;
    int x1 = INT_MIN, y1 = INT_MIN;
    int w, h;
    int has_content = 0;

    // Find sizes of incoming subtitle bitmaps
    for(int n = 0; n < sub->num_rects; n++) {
        AVSubtitleRect *r = sub->rects[n];
        if(r->type != SUBTITLE_BITMAP)
            continue;
        has_content = 1;
        if(r->x < x0)
            x0 = r->x;
        if(r->y < y0)
            y0 = r->y;
        if(r->x + r->w > x1)
            x1 = r->x + r->w;
        if(r->y + r->h > y1)
            y1 = r->y + r->h;
    }

    if(has_content == 0) {
        return NULL;
    }

    w = x1 - x0;
    h = y1 - y0;

    // Surface to render on
    surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
    SDL_FillRect(surface, NULL, 0);

    // Render subimages to the target surface.
    for(int n = 0; n < sub->num_rects; n++) {
        AVSubtitleRect *r = sub->rects[n];
        if(r->type != SUBTITLE_BITMAP)
            continue;
        _ProcessSubImage(surface, r, x0, y0);
    }

    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
    return Kit_CreateSubtitlePacket(KIT_SUBTITLE_PACKET_ADD, start_pts, end_pts, x0, y0, surface);
}

static int ren_get_data_cb(Kit_SubtitleRenderer *ren, Kit_TextureAtlas *atlas, double current_pts) {
    // Read any new packets to atlas
    Kit_SubtitlePacket *packet = NULL;
    while((packet = Kit_PeekDecoderOutput(ren->dec)) != NULL) {
        if(packet->pts_end < current_pts) {
            Kit_AdvanceDecoderOutput(ren->dec);
            Kit_FreeSubtitlePacket(packet);
        }
        if(packet->pts_start > current_pts) {
            break;
        }

        SDL_Rect target;
        target.x = packet->x;
        target.y = packet->y;
        target.w = packet->surface->w;
        target.h = packet->surface->h;
        // NOTE! Surfaces ARE NOT COPIED! WE DIRECTLY HAND OVER THE POINTER TO EXISTING DATA! This is why we cannot
        // free the surface after adding it to atlas.
        Kit_AddAtlasItem(atlas, packet->surface, &target);
        packet->surface = NULL;
    }
    return 0;
}

Kit_SubtitleRenderer* Kit_CreateImageSubtitleRenderer(Kit_Decoder *dec, int w, int h) {
    assert(dec != NULL);
    assert(w >= 0);
    assert(h >= 0);

    // Allocate a new renderer
    Kit_SubtitleRenderer *ren = Kit_CreateSubtitleRenderer(dec);
    if(ren == NULL) {
        return NULL;
    }

    // Only renderer required, no other data.
    ren->ren_render = ren_render_image_cb;
    ren->ren_get_data = ren_get_data_cb;
    ren->ren_close = NULL;
    ren->userdata = NULL;
    return ren;
}
