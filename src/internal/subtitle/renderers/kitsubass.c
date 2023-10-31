#include <assert.h>
#include <stdlib.h>

#include <SDL_surface.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/subtitle/kitatlas.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/subtitle/renderers/kitsubass.h"

typedef struct Kit_ASSSubtitleRenderer {
    ASS_Renderer *renderer;
    ASS_Track *track;
    SDL_mutex *decoder_lock;
} Kit_ASSSubtitleRenderer;

static void Kit_ProcessAssImage(const SDL_Surface *surface, const ASS_Image *img) {
    const unsigned char r = ((img->color) >> 24) & 0xFF;
    const unsigned char g = ((img->color) >> 16) & 0xFF;
    const unsigned char b = ((img->color) >>  8) & 0xFF;
    const unsigned char a = 0xFF - ((img->color) & 0xFF);
    const unsigned char *src = img->bitmap;
    unsigned char *dst = surface->pixels;
    unsigned int x;
    unsigned int y;
    unsigned int rx;

    for(y = 0; y < img->h; y++) {
        for(x = 0; x < img->w; x++) {
            rx = x * 4;
            dst[rx + 0] = r;
            dst[rx + 1] = g;
            dst[rx + 2] = b;
            dst[rx + 3] = (a * src[x]) >> 8;
        }
        src += img->stride;
        dst += surface->pitch;
    }
}

static void ren_render_ass_cb(Kit_SubtitleRenderer *renderer, void *src, double pts, double start, double end) {
    assert(renderer != NULL);
    assert(src != NULL);

    const Kit_ASSSubtitleRenderer *ass_renderer = renderer->userdata;
    const AVSubtitle *sub = src;

    // Read incoming subtitle packets to libASS
    SDL_LockMutex(ass_renderer->decoder_lock);
    long long start_ms = (start + pts) * 1000;
    long long end_ms = end * 1000;
    for(int r = 0; r < sub->num_rects; r++) {
        if(sub->rects[r]->ass == NULL)
            continue;

        // This requires the sub_text_format codec_opt set for ffmpeg
        ass_process_chunk(
                ass_renderer->track,
                sub->rects[r]->ass,
                strlen(sub->rects[r]->ass),
                start_ms,
                end_ms);
    }
    SDL_UnlockMutex(ass_renderer->decoder_lock);
}

static void ren_close_ass_cb(Kit_SubtitleRenderer *renderer) {
    if(!renderer || !renderer->userdata)
        return;
    Kit_ASSSubtitleRenderer *ass_renderer = renderer->userdata;
    ass_free_track(ass_renderer->track);
    ass_renderer_done(ass_renderer->renderer);
    SDL_DestroyMutex(ass_renderer->decoder_lock);
    free(ass_renderer);
}

static int ren_get_ass_data_cb(Kit_SubtitleRenderer *renderer, Kit_TextureAtlas *atlas, SDL_Texture *texture, double current_pts) {
    const Kit_ASSSubtitleRenderer *ass_renderer = renderer->userdata;
    SDL_Surface *dst = NULL;
    int dst_w = 0;
    int dst_h = 0;
    const ASS_Image *src = NULL;
    int change = 0;
    long long now = current_pts * 1000;

    // Tell ASS to render some images
    SDL_LockMutex(ass_renderer->decoder_lock);
    src = ass_render_frame(ass_renderer->renderer, ass_renderer->track, now, &change);
    SDL_UnlockMutex(ass_renderer->decoder_lock);
    if(change == 0) {
        return 0;
    }

    // There was some change, process images and add them to atlas
    Kit_ClearAtlasContent(atlas);
    Kit_CheckAtlasTextureSize(atlas, texture);
    for(; src; src = src->next) {
        if(src->w == 0 || src->h == 0)
            continue;

        // Don't recreate surface if we already have correctly sized one.
        if(dst == NULL || dst_w != src->w || dst_h != src->h) {
            dst_w = src->w;
            dst_h = src->h;
            SDL_FreeSurface(dst);
            dst = SDL_CreateRGBSurfaceWithFormat(0, dst_w, dst_h, 32, SDL_PIXELFORMAT_RGBA32);
        }

        Kit_ProcessAssImage(dst, src);
        SDL_Rect target;
        target.x = src->dst_x;
        target.y = src->dst_y;
        target.w = dst->w;
        target.h = dst->h;
        Kit_AddAtlasItem(atlas, texture, dst, &target);
    }

    SDL_FreeSurface(dst);
    renderer->decoder->clock_pos = current_pts;
    return 0;
}

static void ren_set_ass_size_cb(Kit_SubtitleRenderer *renderer, int w, int h) {
    const Kit_ASSSubtitleRenderer *ass_renderer = renderer->userdata;
    SDL_LockMutex(ass_renderer->decoder_lock);
    ass_set_frame_size(ass_renderer->renderer, w, h);
    SDL_UnlockMutex(ass_renderer->decoder_lock);
}

Kit_SubtitleRenderer* Kit_CreateASSSubtitleRenderer(
    const AVFormatContext *format_ctx, Kit_Decoder *dec, int video_w, int video_h, int screen_w, int screen_h
) {
    assert(dec != NULL);
    assert(video_w >= 0);
    assert(video_h >= 0);
    assert(screen_w >= 0);
    assert(screen_h >= 0);

    Kit_SubtitleRenderer *renderer;
    Kit_ASSSubtitleRenderer *ass_renderer;
    ASS_Renderer *render_handler;
    ASS_Track *render_track;
    SDL_mutex *decoder_lock;

    const Kit_LibraryState *state = Kit_GetLibraryState();
    if(state->libass_handle == NULL) {
        Kit_SetError("Libass library has not been initialized");
        return NULL;
    }
    if((ass_renderer = calloc(1, sizeof(Kit_ASSSubtitleRenderer))) == NULL) {
        Kit_SetError("Unable to allocate ass subtitle renderer");
        goto exit_0;
    }
    if((renderer = Kit_CreateSubtitleRenderer(
        dec,
        ren_render_ass_cb,
        ren_get_ass_data_cb,
        ren_set_ass_size_cb,
        NULL,
        NULL,
        ren_close_ass_cb,
        ass_renderer)) == NULL) {
        goto exit_1;
    }
    if((render_handler = ass_renderer_init(state->libass_handle)) == NULL) {
        Kit_SetError("Unable to initialize libass renderer");
        goto exit_2;
    }
    if((decoder_lock = SDL_CreateMutex()) == NULL) {
        Kit_SetError("Unable to initialize libass decoder lock mutex");
        goto exit_3;
    }

    // Read fonts from attachment streams and give them to libass
    const AVStream *st = NULL;
    for(int j = 0; j < format_ctx->nb_streams; j++) {
        st = format_ctx->streams[j];
        const AVCodecParameters *codec = st->codecpar;
        if(Kit_StreamIsFontAttachment(st)) {
            const AVDictionaryEntry *tag = av_dict_get(
                st->metadata,
                "filename",
                NULL,
                AV_DICT_MATCH_CASE);
            if(tag) {
                ass_add_font(
                    state->libass_handle,
                    tag->value,
                    (char*)codec->extradata,
                    codec->extradata_size);
            }
        }
    }

    // Init libass fonts and window frame size
    ass_set_fonts(
        render_handler,
        NULL, "sans-serif",
        ASS_FONTPROVIDER_AUTODETECT,
        NULL, 1);
    ass_set_storage_size(render_handler, video_w, video_h);
    ass_set_frame_size(render_handler, screen_w, screen_h);
    ass_set_hinting(render_handler, state->font_hinting);

    if((render_track = ass_new_track(state->libass_handle)) == NULL) {
        Kit_SetError("Unable to initialize libass track");
        goto exit_4;
    }
    if(dec->codec_ctx->subtitle_header) {
        ass_process_codec_private(
            render_track,
            (char*)dec->codec_ctx->subtitle_header,
            dec->codec_ctx->subtitle_header_size);
    }

    ass_renderer->renderer = render_handler;
    ass_renderer->track = render_track;
    ass_renderer->decoder_lock = decoder_lock;
    return renderer;

exit_4:
    SDL_DestroyMutex(decoder_lock);
exit_3:
    ass_renderer_done(render_handler);
exit_2:
    Kit_CloseSubtitleRenderer(renderer);
exit_1:
    free(ass_renderer);
exit_0:
    return NULL;
}
