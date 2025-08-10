#include <assert.h>
#include <stdlib.h>

#include <SDL_surface.h>

#include "kitchensink2/internal/kitlibstate.h"
#include "kitchensink2/internal/subtitle/kitatlas.h"
#include "kitchensink2/internal/subtitle/kitsubtitlepacket.h"
#include "kitchensink2/internal/subtitle/renderers/kitsubimage.h"
#include "kitchensink2/kiterror.h"

typedef struct Kit_ImageSubtitleRenderer {
    int video_w;
    int video_h;
    float scale_x;
    float scale_y;
    Kit_PacketBuffer *buffer;
    Kit_SubtitlePacket *in_packet;
    Kit_SubtitlePacket *out_packet;
    SDL_Surface **cached_surfaces;
    unsigned char **cached_items;
    SDL_Rect *cached_src_rects;
    SDL_Rect *cached_dst_rects;
    unsigned int cached_items_size;
} Kit_ImageSubtitleRenderer;

static void ren_render_image_cb(Kit_SubtitleRenderer *renderer, void *sub_src, double pts, double start, double end) {
    assert(renderer != NULL);
    assert(sub_src != NULL);

    Kit_ImageSubtitleRenderer *image_renderer = renderer->userdata;
    const AVSubtitle *sub = sub_src;
    SDL_Surface *tmp = NULL;
    SDL_Surface *dst = NULL;
    const double start_pts = pts + start;
    const double end_pts = pts + end;

    // If this subtitle has no rects, we still need to clear screen from old subs
    if(sub->num_rects == 0) {
        Kit_SetSubtitlePacketData(image_renderer->in_packet, true, start_pts, end_pts, 0, 0, NULL);
        Kit_WritePacketBuffer(image_renderer->buffer, image_renderer->in_packet);
        return;
    }

    // Convert subtitle images from paletted to RGBA8888
    const AVSubtitleRect *r = NULL;
    for(int n = 0; n < sub->num_rects; n++) {
        r = sub->rects[n];
        if(r->type != SUBTITLE_BITMAP)
            continue;

        tmp = SDL_CreateRGBSurfaceWithFormatFrom(r->data[0], r->w, r->h, 8, r->linesize[0], SDL_PIXELFORMAT_INDEX8);
        SDL_SetPaletteColors(tmp->format->palette, (SDL_Color *)r->data[1], 0, 256);
        dst = SDL_CreateRGBSurfaceWithFormat(0, r->w, r->h, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_BlitSurface(tmp, NULL, dst, NULL);
        SDL_FreeSurface(tmp);

        // Create a new packet and write it to output buffer
        Kit_SetSubtitlePacketData(image_renderer->in_packet, false, start_pts, end_pts, r->x, r->y, dst);
        Kit_WritePacketBuffer(image_renderer->buffer, image_renderer->in_packet);
    }
}

static bool Kit_ProcessPacketToAtlas(
    const Kit_ImageSubtitleRenderer *image_renderer, Kit_TextureAtlas *atlas, SDL_Texture *texture, double current_pts
) {
    if(image_renderer->out_packet->surface == NULL && !image_renderer->out_packet->clear) {
        Kit_DelSubtitlePacketRefs(image_renderer->out_packet, true);
        return false;
    }
    if(image_renderer->out_packet->pts_end < current_pts) {
        Kit_DelSubtitlePacketRefs(image_renderer->out_packet, true);
        return false;
    }
    if(image_renderer->out_packet->clear)
        Kit_ClearAtlasContent(atlas);
    if(image_renderer->out_packet->surface != NULL) {
        SDL_Rect target;
        target.x = image_renderer->out_packet->x * image_renderer->scale_x;
        target.y = image_renderer->out_packet->y * image_renderer->scale_y;
        target.w = image_renderer->out_packet->surface->w * image_renderer->scale_x;
        target.h = image_renderer->out_packet->surface->h * image_renderer->scale_y;
        Kit_AddAtlasItem(atlas, texture, image_renderer->out_packet->surface, &target);
    }
    Kit_DelSubtitlePacketRefs(image_renderer->out_packet, true);
    return true;
}

static int ren_get_img_data_cb(
    Kit_SubtitleRenderer *renderer, Kit_TextureAtlas *atlas, SDL_Texture *texture, double current_pts
) {
    const Kit_ImageSubtitleRenderer *image_renderer = renderer->userdata;

    Kit_CheckAtlasTextureSize(atlas, texture);
    Kit_ProcessPacketToAtlas(image_renderer, atlas, texture, current_pts);
    while(Kit_ReadPacketBuffer(image_renderer->buffer, image_renderer->out_packet, 0))
        if(!Kit_ProcessPacketToAtlas(image_renderer, atlas, texture, current_pts))
            break;
    return 0;
}

static void Kit_ClearSubCache(Kit_ImageSubtitleRenderer *image_renderer) {
    for(int i = 0; i < image_renderer->cached_items_size; i++) {
        SDL_FreeSurface(image_renderer->cached_surfaces[i]);
    }
    image_renderer->cached_items_size = 0;
}

static bool Kit_ProcessPacketToCache(Kit_ImageSubtitleRenderer *image_renderer, double current_pts) {
    if(image_renderer->out_packet->surface == NULL && !image_renderer->out_packet->clear) {
        Kit_DelSubtitlePacketRefs(image_renderer->out_packet, true);
        return false;
    }
    if(image_renderer->out_packet->pts_end < current_pts) {
        Kit_DelSubtitlePacketRefs(image_renderer->out_packet, true);
        return false;
    }
    if(image_renderer->out_packet->clear) {
        Kit_ClearSubCache(image_renderer);
    }
    if(image_renderer->out_packet->surface != NULL) {
        const unsigned int index = image_renderer->cached_items_size;
        image_renderer->cached_items_size++;

        image_renderer->cached_items =
            realloc(image_renderer->cached_items, image_renderer->cached_items_size * sizeof(unsigned char *));
        image_renderer->cached_surfaces =
            realloc(image_renderer->cached_surfaces, image_renderer->cached_items_size * sizeof(SDL_Surface *));
        image_renderer->cached_dst_rects =
            realloc(image_renderer->cached_dst_rects, image_renderer->cached_items_size * sizeof(SDL_Rect));
        image_renderer->cached_src_rects =
            realloc(image_renderer->cached_src_rects, image_renderer->cached_items_size * sizeof(SDL_Rect));

        image_renderer->cached_surfaces[index] = image_renderer->out_packet->surface;
        image_renderer->cached_items[index] = image_renderer->out_packet->surface->pixels;
        image_renderer->cached_src_rects[index] = (SDL_Rect){
            .x = 0,
            .y = 0,
            .w = image_renderer->out_packet->surface->w,
            .h = image_renderer->out_packet->surface->h,
        };
        image_renderer->cached_dst_rects[index] = (SDL_Rect){
            .x = image_renderer->out_packet->x * image_renderer->scale_x,
            .y = image_renderer->out_packet->y * image_renderer->scale_y,
            .w = image_renderer->out_packet->surface->w * image_renderer->scale_x,
            .h = image_renderer->out_packet->surface->h * image_renderer->scale_y,
        };
    }
    Kit_DelSubtitlePacketRefs(image_renderer->out_packet, false);
    return true;
}

static int ren_get_img_raw_frames_cb(
    Kit_SubtitleRenderer *renderer, unsigned char ***frames, SDL_Rect **sources, SDL_Rect **targets, double current_pts
) {
    Kit_ImageSubtitleRenderer *image_renderer = renderer->userdata;
    Kit_ProcessPacketToCache(image_renderer, current_pts);
    while(Kit_ReadPacketBuffer(image_renderer->buffer, image_renderer->out_packet, 0))
        if(!Kit_ProcessPacketToCache(image_renderer, current_pts))
            break;

    *frames = image_renderer->cached_items;
    *targets = image_renderer->cached_dst_rects;
    *sources = image_renderer->cached_src_rects;
    return image_renderer->cached_items_size;
}

static void ren_set_img_size_cb(Kit_SubtitleRenderer *ren, int w, int h) {
    Kit_ImageSubtitleRenderer *img_ren = ren->userdata;
    img_ren->scale_x = (float)w / (float)img_ren->video_w;
    img_ren->scale_y = (float)h / (float)img_ren->video_h;
}

static void ren_flush_cb(Kit_SubtitleRenderer *ren) {
    Kit_ImageSubtitleRenderer *image_renderer = ren->userdata;
    Kit_FlushPacketBuffer(image_renderer->buffer);
}

static void ren_signal_cb(Kit_SubtitleRenderer *ren) {
    Kit_ImageSubtitleRenderer *image_renderer = ren->userdata;
    Kit_SignalPacketBuffer(image_renderer->buffer);
}

static void ren_close_img_cb(Kit_SubtitleRenderer *renderer) {
    if(!renderer || !renderer->userdata)
        return;
    Kit_ImageSubtitleRenderer *image_renderer = renderer->userdata;
    if(image_renderer->buffer)
        Kit_FreePacketBuffer(&image_renderer->buffer);
    if(image_renderer->in_packet)
        Kit_FreeSubtitlePacket(&image_renderer->in_packet);
    if(image_renderer->out_packet)
        Kit_FreeSubtitlePacket(&image_renderer->out_packet);
    Kit_ClearSubCache(image_renderer);
    free(image_renderer->cached_items);
    free(image_renderer->cached_dst_rects);
    free(image_renderer->cached_src_rects);
    free(image_renderer->cached_surfaces);
    free(image_renderer);
}

Kit_SubtitleRenderer *
Kit_CreateImageSubtitleRenderer(Kit_Decoder *dec, int video_w, int video_h, int screen_w, int screen_h) {
    assert(dec != NULL);
    assert(video_w >= 0);
    assert(video_h >= 0);
    assert(screen_w >= 0);
    assert(screen_h >= 0);

    Kit_LibraryState *state = Kit_GetLibraryState();
    Kit_SubtitleRenderer *renderer;
    Kit_ImageSubtitleRenderer *image_renderer;
    Kit_PacketBuffer *buffer;
    Kit_SubtitlePacket *in_packet;
    Kit_SubtitlePacket *out_packet;

    if((image_renderer = calloc(1, sizeof(Kit_ImageSubtitleRenderer))) == NULL) {
        Kit_SetError("Unable to allocate image subtitle renderer");
        goto exit_0;
    }
    if((renderer = Kit_CreateSubtitleRenderer(
            dec,
            ren_render_image_cb,
            ren_get_img_data_cb,
            ren_get_img_raw_frames_cb,
            ren_set_img_size_cb,
            ren_flush_cb,
            ren_signal_cb,
            ren_close_img_cb,
            image_renderer
        )) == NULL) {
        goto exit_1;
    }
    if((buffer = Kit_CreatePacketBuffer(
            state->subtitle_frame_buffer_size,
            (buf_obj_alloc)Kit_CreateSubtitlePacket,
            (buf_obj_unref)Kit_DelSubtitlePacketRefs,
            (buf_obj_free)Kit_FreeSubtitlePacket,
            (buf_obj_move)Kit_MoveSubtitlePacketRefs,
            (buf_obj_ref)Kit_CreateSubtitlePacketRef,
            NULL
        )) == NULL) {
        Kit_SetError("Unable to create an output buffer for subtitle renderer");
        goto exit_2;
    }
    if((in_packet = Kit_CreateSubtitlePacket()) == NULL) {
        Kit_SetError("Unable to allocate a input packet for subtitle renderer");
        goto exit_3;
    }
    if((out_packet = Kit_CreateSubtitlePacket()) == NULL) {
        Kit_SetError("Unable to allocate a output packet for subtitle renderer");
        goto exit_4;
    }

    // Only renderer required, no other data.
    image_renderer->buffer = buffer;
    image_renderer->in_packet = in_packet;
    image_renderer->out_packet = out_packet;
    image_renderer->video_w = video_w;
    image_renderer->video_h = video_h;
    image_renderer->scale_x = (float)screen_w / (float)video_w;
    image_renderer->scale_y = (float)screen_h / (float)video_h;
    image_renderer->cached_items = NULL;
    image_renderer->cached_surfaces = NULL;
    image_renderer->cached_dst_rects = NULL;
    image_renderer->cached_src_rects = NULL;
    image_renderer->cached_items_size = 0;
    return renderer;

exit_4:
    Kit_FreeSubtitlePacket(&in_packet);
exit_3:
    Kit_FreePacketBuffer(&buffer);
exit_2:
    Kit_CloseSubtitleRenderer(renderer);
exit_1:
    free(image_renderer);
exit_0:
    return NULL;
}
