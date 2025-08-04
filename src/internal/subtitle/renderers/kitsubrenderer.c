#include <stdlib.h>

#include "kitchensink/internal/subtitle/kitsubtitlepacket.h"
#include "kitchensink/internal/subtitle/renderers/kitsubrenderer.h"
#include "kitchensink/kiterror.h"

Kit_SubtitleRenderer *Kit_CreateSubtitleRenderer(
    Kit_Decoder *decoder,
    renderer_render_cb render_cb,
    renderer_get_data_cb get_data_cb,
    renderer_get_raw_frames_cb get_raw_frames_cb,
    renderer_set_size_cb set_size_cb,
    renderer_flush_cb flush_cb,
    renderer_signal_cb signal_cb,
    renderer_close_cb close_cb,
    void *userdata
) {
    Kit_SubtitleRenderer *renderer = calloc(1, sizeof(Kit_SubtitleRenderer));
    if(renderer == NULL) {
        Kit_SetError("Unable to allocate kit subtitle renderer");
        return NULL;
    }
    renderer->decoder = decoder;
    renderer->render_cb = render_cb;
    renderer->get_raw_frames_cb = get_raw_frames_cb;
    renderer->close_cb = close_cb;
    renderer->get_data_cb = get_data_cb;
    renderer->set_size_cb = set_size_cb;
    renderer->flush_cb = flush_cb;
    renderer->signal_cb = signal_cb;
    renderer->userdata = userdata;
    return renderer;
}

void Kit_RunSubtitleRenderer(Kit_SubtitleRenderer *renderer, void *src, double pts, double start, double end) {
    if(renderer == NULL)
        return;
    if(renderer->render_cb != NULL)
        renderer->render_cb(renderer, src, pts, start, end);
}

void Kit_FlushSubtitleRendererBuffers(Kit_SubtitleRenderer *renderer) {
    if(renderer->flush_cb)
        renderer->flush_cb(renderer);
}

void Kit_SignalSubtitleRenderer(Kit_SubtitleRenderer *renderer) {
    if(renderer->signal_cb)
        renderer->signal_cb(renderer);
}

int Kit_GetSubtitleRendererSDLTexture(
    Kit_SubtitleRenderer *renderer, Kit_TextureAtlas *atlas, SDL_Texture *texture, double current_pts
) {
    if(renderer == NULL)
        return 0;
    if(renderer->get_data_cb != NULL)
        return renderer->get_data_cb(renderer, atlas, texture, current_pts);
    return 0;
}

int Kit_GetSubtitleRendererRawFrames(
    Kit_SubtitleRenderer *renderer, unsigned char ***frames, SDL_Rect **sources, SDL_Rect **targets, double current_pts
) {
    if(renderer == NULL)
        return 0;
    if(renderer->get_raw_frames_cb != NULL)
        return renderer->get_raw_frames_cb(renderer, frames, sources, targets, current_pts);
    return 0;
}

void Kit_SetSubtitleRendererSize(Kit_SubtitleRenderer *renderer, int w, int h) {
    if(renderer == NULL)
        return;
    if(renderer->set_size_cb != NULL)
        renderer->set_size_cb(renderer, w, h);
}

void Kit_CloseSubtitleRenderer(Kit_SubtitleRenderer *renderer) {
    if(renderer == NULL)
        return;
    if(renderer->close_cb != NULL)
        renderer->close_cb(renderer);
    free(renderer);
}
