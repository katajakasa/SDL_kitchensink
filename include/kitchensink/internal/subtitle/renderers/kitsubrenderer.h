#ifndef KITSUBRENDERER_H
#define KITSUBRENDERER_H

#include <SDL_render.h>

#include "kitchensink/kitsource.h"
#include "kitchensink/internal/subtitle/kitatlas.h"
#include "kitchensink/internal/kitdecoder.h"

typedef struct Kit_SubtitleRenderer Kit_SubtitleRenderer;

typedef void (*renderer_render_cb)(Kit_SubtitleRenderer *ren, void *src, double pts, double start, double end);
typedef int (*renderer_get_data_cb)(Kit_SubtitleRenderer *ren, Kit_TextureAtlas *atlas, SDL_Texture *texture, double current_pts);
typedef void (*renderer_set_size_cb)(Kit_SubtitleRenderer *ren, int w, int h);
typedef void (*renderer_close_cb)(Kit_SubtitleRenderer *ren);

struct Kit_SubtitleRenderer {
    Kit_Decoder *decoder;
    void *userdata;
    renderer_render_cb render_cb; ///< Subtitle rendering function callback
    renderer_get_data_cb get_data_cb; ///< Subtitle data getter function callback
    renderer_set_size_cb set_size_cb; ///< Screen size setter function callback
    renderer_close_cb close_cb; ///< Subtitle renderer close function callback
};

KIT_LOCAL Kit_SubtitleRenderer* Kit_CreateSubtitleRenderer(
        Kit_Decoder *decoder,
        renderer_render_cb render_cb,
        renderer_get_data_cb get_data_cb,
        renderer_set_size_cb set_size_cb,
        renderer_close_cb close_cb,
        void *userdata
);
KIT_LOCAL void Kit_RunSubtitleRenderer(
        Kit_SubtitleRenderer *renderer, void *src, double pts, double start, double end);
KIT_LOCAL int Kit_GetSubtitleRendererData(
        Kit_SubtitleRenderer *renderer, Kit_TextureAtlas *atlas, SDL_Texture *texture, double current_pts);
KIT_LOCAL void Kit_SetSubtitleRendererSize(
        Kit_SubtitleRenderer *renderer, int w, int h);
KIT_LOCAL void Kit_CloseSubtitleRenderer(
        Kit_SubtitleRenderer *renderer);

#endif // KITSUBRENDERER_H
