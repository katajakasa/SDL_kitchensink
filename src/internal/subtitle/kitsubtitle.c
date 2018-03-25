#include <assert.h>

#include <SDL2/SDL.h>
#include <libavformat/avformat.h>

#include "kitchensink/internal/utils/kitlog.h"

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/utils/kitlog.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/subtitle/kitsubtitlepacket.h"
#include "kitchensink/internal/subtitle/kitsubtitle.h"
#include "kitchensink/internal/subtitle/renderers/kitsubimage.h"
#include "kitchensink/internal/subtitle/renderers/kitsubass.h"
#include "kitchensink/internal/subtitle/renderers/kitsubrenderer.h"
#include "kitchensink/internal/utils/kithelpers.h"


#define KIT_SUBTITLE_OUT_SIZE 1024

typedef struct Kit_SubtitleDecoder {
    Kit_SubtitleFormat *format;
    Kit_SubtitleRenderer *renderer;
    AVSubtitle scratch_frame;
    int w;
    int h;
    int output_state;
    SDL_Surface *tmp_buffer;
} Kit_SubtitleDecoder;


static void free_out_subtitle_packet_cb(void *packet) {
    Kit_FreeSubtitlePacket((Kit_SubtitlePacket*)packet);
}

static int dec_decode_subtitle_cb(Kit_Decoder *dec, AVPacket *in_packet) {
    assert(dec != NULL);
    assert(in_packet != NULL);

    Kit_SubtitleDecoder *subtitle_dec = dec->userdata;
    int frame_finished;
    int len;

    if(in_packet->size > 0) {
        len = avcodec_decode_subtitle2(dec->codec_ctx, &subtitle_dec->scratch_frame, &frame_finished, in_packet);
        if(len < 0) {
            return 1;
        }

        if(frame_finished) {
            // Start and end presentation timestamps for subtitle frame
            double pts = 0;
            if(in_packet->pts != AV_NOPTS_VALUE) {
                pts = in_packet->pts;
                pts *= av_q2d(dec->format_ctx->streams[dec->stream_index]->time_base);
            }
            double start = pts + (subtitle_dec->scratch_frame.start_display_time / 1000.0f);
            double end = pts + (subtitle_dec->scratch_frame.end_display_time / 1000.0f);

            // Create a packet. This should be filled by renderer.
            Kit_SubtitlePacket *out_packet = Kit_RunSubtitleRenderer(
                subtitle_dec->renderer, &subtitle_dec->scratch_frame, start, end);
            if(out_packet != NULL) {
                Kit_WriteDecoderOutput(dec, out_packet);
            }

            // Free subtitle since it has now been handled
            avsubtitle_free(&subtitle_dec->scratch_frame);
        }

        LOGFLUSH();
    }

    return 1;
}

static void dec_close_subtitle_cb(Kit_Decoder *dec) {
    if(dec == NULL) return;
    Kit_SubtitleDecoder *subtitle_dec = dec->userdata;
    SDL_FreeSurface(subtitle_dec->tmp_buffer);
    Kit_CloseSubtitleRenderer(subtitle_dec->renderer);
    free(subtitle_dec);
}

Kit_Decoder* Kit_CreateSubtitleDecoder(const Kit_Source *src, Kit_SubtitleFormat *format, int w, int h) {
    assert(src != NULL);
    assert(format != NULL);
    if(src->subtitle_stream_index < 0) {
        return NULL;
    }

    // First the generic decoder component
    Kit_Decoder *dec = Kit_CreateDecoder(
        src, src->subtitle_stream_index,
        KIT_SUBTITLE_OUT_SIZE,
        free_out_subtitle_packet_cb);
    if(dec == NULL) {
        goto exit_0;
    }

    // Set format. Note that is_enabled may be changed below ...
    format->is_enabled = true;
    format->stream_index = src->subtitle_stream_index;
    format->format = SDL_PIXELFORMAT_RGBA32; // Always this

    // ... then allocate the subtitle decoder
    Kit_SubtitleDecoder *subtitle_dec = calloc(1, sizeof(Kit_SubtitleDecoder));
    if(subtitle_dec == NULL) {
        goto exit_1;
    }

    // For subtitles, we need a renderer for the stream. Pick one based on codec ID.
    Kit_SubtitleRenderer *ren = NULL;
    switch(dec->codec_ctx->codec_id) {
        case AV_CODEC_ID_SSA:
        case AV_CODEC_ID_ASS:
            ren = Kit_CreateASSSubtitleRenderer(dec, w, h);
            break;
        case AV_CODEC_ID_DVD_SUBTITLE:
        case AV_CODEC_ID_DVB_SUBTITLE:
        case AV_CODEC_ID_HDMV_PGS_SUBTITLE:
        case AV_CODEC_ID_XSUB:
            format->is_enabled = false;
            break;
        default:
            format->is_enabled = false;
            break;
    }
    if(ren == NULL) {
        goto exit_2;
    }

    // Allocate temporary screen-sized subtitle buffer
    SDL_Surface *tmp_buffer = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if(tmp_buffer == NULL) {
        goto exit_3;
    }
    SDL_FillRect(tmp_buffer, NULL, 0);

    // Set callbacks and userdata, and we're go
    subtitle_dec->format = format;
    subtitle_dec->renderer = ren;
    subtitle_dec->w = w;
    subtitle_dec->h = h;
    subtitle_dec->tmp_buffer = tmp_buffer;
    subtitle_dec->output_state = 0;
    dec->dec_decode = dec_decode_subtitle_cb;
    dec->dec_close = dec_close_subtitle_cb;
    dec->userdata = subtitle_dec;
    return dec;

exit_3:
    Kit_CloseSubtitleRenderer(ren);
exit_2:
    free(subtitle_dec);
exit_1:
    Kit_CloseDecoder(dec);
exit_0:
    return NULL;
}


typedef struct {
    double sync_ts;
    SDL_Surface *surface;
    int rendered;
} tmp_sub_image;


static void _merge_subtitle_texture(void *ptr, void *userdata) {
    tmp_sub_image *img = userdata;
    Kit_SubtitlePacket *packet = ptr;

    // Make sure current time is within presentation range
    if(packet->pts_start >= img->sync_ts)
        return;
    if(packet->pts_end <= img->sync_ts)
        return;

    // Tell the renderer function that we did something here
    img->rendered = 1;

    // Blit source whole source surface to target surface in requested coords
    SDL_Rect dst_rect;
    dst_rect.x = packet->x;
    dst_rect.y = packet->y;
    SDL_BlitSurface(packet->surface, NULL, img->surface, &dst_rect);
}


int Kit_GetSubtitleDecoderData(Kit_Decoder *dec, SDL_Texture *texture) {
    assert(dec != NULL);
    assert(texture != NULL);

    Kit_SubtitleDecoder *subtitle_dec = dec->userdata;

    double sync_ts = _GetSystemTime() - dec->clock_sync;

    // If we rendered on last frame, clear the buffer
    if(subtitle_dec->output_state == 1) {
        SDL_FillRect(subtitle_dec->tmp_buffer, NULL, 0);
    }

    // Blit all subtitle image rectangles to master buffer
    tmp_sub_image img;
    img.sync_ts = sync_ts;
    img.surface = subtitle_dec->tmp_buffer;
    img.rendered = 0;
    Kit_ForEachDecoderOutput(dec, _merge_subtitle_texture, (void*)&img);

    // Clear out old packets
    Kit_SubtitlePacket *packet = NULL;
    while((packet = Kit_PeekDecoderOutput(dec)) != NULL) {
        if(packet->pts_end >= sync_ts)
            break;
        Kit_AdvanceDecoderOutput(dec);
        free_out_subtitle_packet_cb(packet);
    }

    // If nothing was rendered now or last frame, just return. No need to update texture.
    dec->clock_pos = sync_ts;
    if(img.rendered == 0 && subtitle_dec->output_state == 0) {
        return 1;
    }
    subtitle_dec->output_state = img.rendered;

    // Update output texture with current buffered image
    SDL_UpdateTexture(
        texture, NULL,
        subtitle_dec->tmp_buffer->pixels,
        subtitle_dec->tmp_buffer->pitch);

    // all done!
    return 0;
}
