#include <assert.h>

#include <SDL2/SDL.h>
#include <libavformat/avformat.h>

#include "kitchensink/internal/utils/kitlog.h"

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/subtitle/kitsubtitle.h"
#include "kitchensink/internal/subtitle/renderers/kitsubimage.h"
#include "kitchensink/internal/subtitle/renderers/kitsubass.h"
#include "kitchensink/internal/subtitle/renderers/kitsubrenderer.h"
#include "kitchensink/internal/utils/kithelpers.h"


#define KIT_SUBTITLE_OUT_SIZE 32

typedef struct Kit_SubtitleDecoder {
    Kit_SubtitleFormat *format;
    Kit_SubtitleRenderer *renderer;
    AVSubtitle scratch_frame;
    int w;
    int h;
    int output_state;
} Kit_SubtitleDecoder;

typedef struct Kit_SubtitlePacket {
    double pts_start;
    double pts_end;
    SDL_Surface *surface;
} Kit_SubtitlePacket;


static Kit_SubtitlePacket* _CreateSubtitlePacket(double pts_start, double pts_end, SDL_Surface *surface) {
    Kit_SubtitlePacket *p = calloc(1, sizeof(Kit_SubtitlePacket));
    p->pts_start = pts_start;
    p->pts_end = pts_end;
    p->surface = surface;
    return p;
}

static void free_out_subtitle_packet_cb(void *packet) {
    Kit_SubtitlePacket *p = packet;
    SDL_FreeSurface(p->surface);
    free(p);
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

            // Surface to render on
            SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
                0, subtitle_dec->w, subtitle_dec->h, 32, SDL_PIXELFORMAT_RGBA32);
            memset(surface->pixels, 0, surface->pitch * surface->h);
    
            // Create a packet. This should be filled by renderer.
            Kit_SubtitlePacket *out_packet = _CreateSubtitlePacket(start, end, surface);
            int ret = Kit_RunSubtitleRenderer(
                subtitle_dec->renderer, &subtitle_dec->scratch_frame, start, surface);
            if(ret == -1) {
                // Renderer failed, free packet.
                free_out_subtitle_packet_cb(out_packet);
            } else {
                Kit_WriteDecoderOutput(dec, out_packet);
            }

            // Free subtitle since it has now been handled
            avsubtitle_free(&subtitle_dec->scratch_frame);
        }
    }

    return 1;
}

static void dec_close_subtitle_cb(Kit_Decoder *dec) {
    if(dec == NULL) return;
    Kit_SubtitleDecoder *subtitle_dec = dec->userdata;
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

    // Set callbacks and userdata, and we're go
    subtitle_dec->format = format;
    subtitle_dec->renderer = ren;
    subtitle_dec->w = w;
    subtitle_dec->h = h;
    dec->dec_decode = dec_decode_subtitle_cb;
    dec->dec_close = dec_close_subtitle_cb;
    dec->userdata = subtitle_dec;
    return dec;

exit_2:
    free(subtitle_dec);
exit_1:
    Kit_CloseDecoder(dec);
exit_0:
    return NULL;
}

int Kit_GetSubtitleDecoderData(Kit_Decoder *dec, SDL_Texture *texture) {
    assert(dec != NULL);
    assert(texture != NULL);

    double sync_ts = _GetSystemTime() - dec->clock_sync;
    char *clear_scr;
    
    Kit_SubtitleDecoder *subtitle_dec = dec->userdata;
    Kit_SubtitlePacket *packet = Kit_PeekDecoderOutput(dec);
    if(packet == NULL) {
        goto exit_0;
    }
    if(packet->pts_start > sync_ts) {
        goto exit_0;
    }
    if(packet->pts_end < sync_ts) {
        goto exit_1;
    }

    // Update the texture with our subtitle frame
    SDL_UpdateTexture(
        texture, NULL,
        packet->surface->pixels,
        packet->surface->pitch);

    dec->clock_pos = sync_ts;
    return 0;

    // All done.
exit_1:
    Kit_AdvanceDecoderOutput(dec);
    free_out_subtitle_packet_cb(packet);
exit_0:
    // Clear subtitle frame
    clear_scr = calloc(1, subtitle_dec->h * subtitle_dec->w * 4);
    SDL_UpdateTexture(texture, NULL, clear_scr, subtitle_dec->w * 4);
    free(clear_scr);

    return 0;
}
