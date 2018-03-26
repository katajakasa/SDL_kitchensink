#include <assert.h>
#include <stdlib.h>

#include <ass/ass.h>
#include <SDL2/SDL_surface.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/utils/kitlog.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/subtitle/kitsubtitlepacket.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/subtitle/renderers/kitsubass.h"

// For compatibility
#ifndef ASS_FONTPROVIDER_AUTODETECT
#define ASS_FONTPROVIDER_AUTODETECT 1
#endif

typedef struct Kit_ASSSubtitleRenderer {
    ASS_Renderer *renderer;
    ASS_Track *track;
} Kit_ASSSubtitleRenderer;

static void _ProcessAssImage(SDL_Surface *surface, const ASS_Image *img, int min_x, int min_y) {
    unsigned char r = ((img->color) >> 24) & 0xFF;
    unsigned char g = ((img->color) >> 16) & 0xFF;
    unsigned char b = ((img->color) >>  8) & 0xFF;
    unsigned char a = (img->color) & 0xFF;
    unsigned char *src = img->bitmap;
    unsigned char *dst = surface->pixels;
    unsigned int pos_x = img->dst_x - min_x;
    unsigned int pos_y = img->dst_y - min_y;
    unsigned int an, ao, x, y, x_off;
    dst += pos_y * surface->pitch;

    for(y = 0; y < img->h; y++) {
        for(x = 0; x < img->w; x++) {
            x_off = (pos_x + x) * 4;
            an = ((255 - a) * src[x]) >> 8; // New alpha
            ao = dst[x_off + 3]; // Original alpha
            if(ao == 0) {
                dst[x_off + 0] = r;
                dst[x_off + 1] = g;
                dst[x_off + 2] = b;
                dst[x_off + 3] = an;
            } else {
                dst[x_off + 3] = 255 - (255 - dst[x_off + 3]) * (255 - an) / 255;
                if(dst[x_off + 3] != 0) {
                    dst[x_off + 0] = (dst[x_off + 0] * ao * (255-an) / 255 + r * an ) / dst[x_off + 3];
                    dst[x_off + 1] = (dst[x_off + 1] * ao * (255-an) / 255 + g * an ) / dst[x_off + 3];
                    dst[x_off + 2] = (dst[x_off + 2] * ao * (255-an) / 255 + b * an ) / dst[x_off + 3];
                }
            }
        }
        src += img->stride;
        dst += surface->pitch;
    }
}

static Kit_SubtitlePacket* ren_render_ass_cb(Kit_SubtitleRenderer *ren, void *src, double start_pts, double end_pts) {
    assert(ren != NULL);
    assert(src != NULL);

    Kit_ASSSubtitleRenderer *ass_ren = ren->userdata;
    AVSubtitle *sub = src;
    SDL_Surface *surface = NULL;
    ASS_Image *image = NULL;
    ASS_Image *wt_image = NULL;
    unsigned int now = start_pts * 1000;
    int change = 0;
    int x0 = INT_MAX, y0 = INT_MAX;
    int x1 = 0, y1 = 0;
    int w, h;

    // Read incoming subtitle packets to libASS
    for(int r = 0; r < sub->num_rects; r++) {
        if(sub->rects[r]->ass == NULL)
            continue;
        ass_process_data(ass_ren->track, sub->rects[r]->ass, strlen(sub->rects[r]->ass));
    }

    // Ask libass to render frames. If there are no changes since last render, stop here.
    wt_image = image = ass_render_frame(ass_ren->renderer, ass_ren->track, now, &change);
    if(change == 0) {
        return NULL;
    }

    // Find dimensions
    for(image = wt_image; image; image = image->next) {
        if(image->dst_x < x0)
            x0 = image->dst_x;
        if(image->dst_y < y0)
            y0 = image->dst_y;
        if(image->dst_x + image->w > x1)
            x1 = image->dst_x + image->w;
        if(image->dst_y + image->h > y1)
            y1 = image->dst_y + image->h;
    }
    w = x1 - x0;
    h = y1 - y0;

    // Surface to render on
    surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
    SDL_FillRect(surface, NULL, 0);

    // Render subimages to the target surface.
    for(image = wt_image; image; image = image->next) {
        if(image->w == 0 || image->h == 0)
            continue;
        _ProcessAssImage(surface, image, x0, y0);
    }

    // We tell subtitle handler to clear output before adding this frame.
    return Kit_CreateSubtitlePacket(start_pts, end_pts, x0, y0, surface);
}

static void ren_close_ass_cb(Kit_SubtitleRenderer *ren) {
    if(ren == NULL) return;

    Kit_ASSSubtitleRenderer *ass_ren = ren->userdata;
    ass_renderer_done(ass_ren->renderer);
    free(ass_ren);
}

Kit_SubtitleRenderer* Kit_CreateASSSubtitleRenderer(const Kit_Decoder *dec, int w, int h) {
    assert(dec != NULL);
    assert(w >= 0);
    assert(h >= 0);

    // Make sure that libass library has been initialized + get handle
    Kit_LibraryState *state = Kit_GetLibraryState();
    if(state->libass_handle == NULL) {
        Kit_SetError("Libass library has not been initialized");
        return NULL;
    }

    // First allocate the generic decoder component
    Kit_SubtitleRenderer *ren = Kit_CreateSubtitleRenderer();
    if(ren == NULL) {
        goto exit_0;
    }

    // Next, allocate ASS subtitle renderer context.
    Kit_ASSSubtitleRenderer *ass_ren = calloc(1, sizeof(Kit_ASSSubtitleRenderer));
    if(ass_ren == NULL) {
        goto exit_1;
    }

    // Initialize libass renderer
    ASS_Renderer *ass_renderer = ass_renderer_init(state->libass_handle);
    if(ass_renderer == NULL) {
        Kit_SetError("Unable to initialize libass renderer");
        goto exit_2;
    }

    // Read fonts from attachment streams and give them to libass
    for(int j = 0; j < dec->format_ctx->nb_streams; j++) {
        AVStream *st = dec->format_ctx->streams[j];
        if(st->codec->codec_type == AVMEDIA_TYPE_ATTACHMENT && attachment_is_font(st)) {
            const AVDictionaryEntry *tag = av_dict_get(
                st->metadata,
                "filename",
                NULL,
                AV_DICT_MATCH_CASE);
            if(tag) {
                ass_add_font(
                    state->libass_handle,
                    tag->value, 
                    (char*)st->codec->extradata,
                    st->codec->extradata_size);
            }
        }
    }

    // Init libass fonts and window frame size
    ass_set_fonts(
        ass_renderer,
        NULL, "sans-serif",
        ASS_FONTPROVIDER_AUTODETECT,
        NULL, 1);
    ass_set_frame_size(ass_renderer, w, h);
    ass_set_hinting(ass_renderer, ASS_HINTING_NONE);

    // Initialize libass track
    ASS_Track *ass_track = ass_new_track(state->libass_handle);
    if(ass_track == NULL) {
        Kit_SetError("Unable to initialize libass track");
        goto exit_3;
    }

    // Set up libass track headers (ffmpeg provides these)
    if(dec->codec_ctx->subtitle_header) {
        ass_process_codec_private(
            ass_track,
            (char*)dec->codec_ctx->subtitle_header,
            dec->codec_ctx->subtitle_header_size);
    }

    // Set callbacks and userdata, and we're go
    ass_ren->renderer = ass_renderer;
    ass_ren->track = ass_track;
    ren->ren_render = ren_render_ass_cb;
    ren->ren_close = ren_close_ass_cb;
    ren->userdata = ass_ren;
    return ren;

exit_3:
    ass_renderer_done(ass_renderer);
exit_2:
    free(ass_ren);
exit_1:
    Kit_CloseSubtitleRenderer(ren);
exit_0:
    return NULL;
}
