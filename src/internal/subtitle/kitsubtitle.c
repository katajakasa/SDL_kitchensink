#include <assert.h>

#include <SDL.h>
#include <libavformat/avformat.h>

#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/subtitle/kitatlas.h"
#include "kitchensink/internal/subtitle/kitsubtitle.h"
#include "kitchensink/internal/subtitle/kitsubtitlepacket.h"
#include "kitchensink/internal/subtitle/renderers/kitsubass.h"
#include "kitchensink/internal/subtitle/renderers/kitsubimage.h"
#include "kitchensink/internal/subtitle/renderers/kitsubrenderer.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/kitlib.h"

typedef struct Kit_SubtitleDecoder {
    Kit_SubtitleRenderer *renderer;
    AVSubtitle scratch_frame;
    Kit_TextureAtlas *atlas;
    Kit_SubtitleOutputFormat output;
} Kit_SubtitleDecoder;

static void dec_read_subtitle(const Kit_Decoder *decoder, int64_t packet_pts) {
    Kit_SubtitleDecoder *subtitle_decoder = decoder->userdata;

    // Start and end presentation timestamps for subtitle frame
    double pts = 0;
    if(packet_pts != AV_NOPTS_VALUE)
        pts = packet_pts * av_q2d(decoder->stream->time_base);

    // If subtitle has no ending time, we set some safety value.
    if(subtitle_decoder->scratch_frame.end_display_time == UINT_MAX)
        subtitle_decoder->scratch_frame.end_display_time = 30000;

    const double start = subtitle_decoder->scratch_frame.start_display_time / 1000.0F;
    const double end = subtitle_decoder->scratch_frame.end_display_time / 1000.0F;

    // Create a packet. This should be filled by renderer.
    Kit_RunSubtitleRenderer(subtitle_decoder->renderer, &subtitle_decoder->scratch_frame, pts, start, end);
}

static void dec_flush_subtitle_cb(Kit_Decoder *decoder) {
    assert(decoder);
    Kit_SubtitleDecoder *subtitle_decoder = decoder->userdata;
    Kit_FlushSubtitleRendererBuffers(subtitle_decoder->renderer);
}

static void dec_signal_subtitle_cb(Kit_Decoder *decoder) {
    assert(decoder);
    Kit_SubtitleDecoder *subtitle_decoder = decoder->userdata;
    Kit_SignalSubtitleRenderer(subtitle_decoder->renderer);
}

static Kit_DecoderInputResult dec_input_subtitle_cb(const Kit_Decoder *dec, const AVPacket *in_packet) {
    assert(dec);

    Kit_SubtitleDecoder *subtitle_decoder = dec->userdata;
    int frame_finished;

    if(in_packet == NULL)
        return KIT_DEC_INPUT_EOF;
    if(in_packet->size <= 0)
        return KIT_DEC_INPUT_OK;
    if(avcodec_decode_subtitle2(dec->codec_ctx, &subtitle_decoder->scratch_frame, &frame_finished, in_packet) < 0)
        return KIT_DEC_INPUT_OK;
    if(frame_finished) {
        dec_read_subtitle(dec, in_packet->pts);
        avsubtitle_free(&subtitle_decoder->scratch_frame);
    }
    return KIT_DEC_INPUT_OK;
}

static bool dec_decode_subtitle_cb(const Kit_Decoder *dec, double *pts) {
    *pts = -1.0;
    return false;
}

static void dec_get_subtitle_buffers_cb(const Kit_Decoder *ref, unsigned int *length, unsigned int *capacity) {
    assert(ref);
    assert(ref->userdata);
    Kit_SubtitleDecoder *subtitle_decoder = ref->userdata;
    if(length != NULL)
        *length = subtitle_decoder->atlas->cur_items;
    if(capacity != NULL)
        *capacity = subtitle_decoder->atlas->max_items;
}

static void dec_close_subtitle_cb(Kit_Decoder *ref) {
    if(ref == NULL)
        return;
    assert(ref->userdata);
    Kit_SubtitleDecoder *subtitle_dec = ref->userdata;
    avsubtitle_free(&subtitle_dec->scratch_frame);
    if(subtitle_dec->atlas != NULL)
        Kit_FreeAtlas(subtitle_dec->atlas);
    if(subtitle_dec->renderer != NULL)
        Kit_CloseSubtitleRenderer(subtitle_dec->renderer);
    free(subtitle_dec);
}

int Kit_GetSubtitleDecoderOutputFormat(const Kit_Decoder *decoder, Kit_SubtitleOutputFormat *output) {
    if(decoder == NULL) {
        memset(output, 0, sizeof(Kit_SubtitleOutputFormat));
        return 1;
    }
    Kit_SubtitleDecoder *subtitle_decoder = decoder->userdata;
    memcpy(output, &subtitle_decoder->output, sizeof(Kit_SubtitleOutputFormat));
    return 0;
}

Kit_Decoder *Kit_CreateSubtitleDecoder(
    const Kit_Source *src,
    Kit_Timer *sync_timer,
    int stream_index,
    int video_w,
    int video_h,
    int screen_w,
    int screen_h
) {
    assert(src != NULL);
    assert(video_w >= 0);
    assert(video_h >= 0);
    assert(screen_w >= 0);
    assert(screen_h >= 0);

    const Kit_LibraryState *state = Kit_GetLibraryState();
    const AVFormatContext *format_ctx = src->format_ctx;
    AVStream *stream = NULL;
    Kit_SubtitleDecoder *subtitle_decoder = NULL;
    Kit_Decoder *decoder = NULL;
    Kit_SubtitleOutputFormat output;
    Kit_SubtitleRenderer *renderer = NULL;
    Kit_TextureAtlas *atlas = NULL;

    // Find and set up stream.
    if(stream_index < 0 || stream_index >= format_ctx->nb_streams) {
        Kit_SetError("Invalid subtitle stream index %d", stream_index);
        return NULL;
    }
    stream = format_ctx->streams[stream_index];

    if((subtitle_decoder = calloc(1, sizeof(Kit_SubtitleDecoder))) == NULL) {
        Kit_SetError("Unable to allocate audio decoder for stream %d", stream_index);
        goto exit_0;
    }
    if((decoder = Kit_CreateDecoder(
            stream,
            sync_timer,
            state->thread_count,
            KIT_HWDEVICE_TYPE_ALL,
            dec_input_subtitle_cb,
            dec_decode_subtitle_cb,
            dec_flush_subtitle_cb,
            dec_signal_subtitle_cb,
            dec_close_subtitle_cb,
            dec_get_subtitle_buffers_cb,
            subtitle_decoder
        )) == NULL) {
        // No need to Kit_SetError, it will be set in Kit_CreateDecoder.
        goto exit_1;
    }
    if((atlas = Kit_CreateAtlas()) == NULL) {
        Kit_SetError("Unable to allocate subtitle texture atlas for stream %d", stream_index);
        goto exit_2;
    }

    // Note -- format is always RGBA32, there is no point in using anything else due to libass favoring this.
    memset(&output, 0, sizeof(Kit_SubtitleOutputFormat));
    output.format = SDL_PIXELFORMAT_RGBA32;

    // For subtitles, we need a renderer for the stream. Pick one based on codec ID.
    switch(decoder->codec_ctx->codec_id) {
        case AV_CODEC_ID_TEXT:
        case AV_CODEC_ID_HDMV_TEXT_SUBTITLE:
        case AV_CODEC_ID_SRT:
        case AV_CODEC_ID_SUBRIP:
        case AV_CODEC_ID_SSA:
        case AV_CODEC_ID_ASS:
            if(state->init_flags & KIT_INIT_ASS) {
                renderer = Kit_CreateASSSubtitleRenderer(format_ctx, decoder, video_w, video_h, screen_w, screen_h);
            }
            break;
        case AV_CODEC_ID_DVD_SUBTITLE:
        case AV_CODEC_ID_DVB_SUBTITLE:
        case AV_CODEC_ID_HDMV_PGS_SUBTITLE:
        case AV_CODEC_ID_XSUB:
        case AV_CODEC_ID_DVB_TELETEXT:
            renderer = Kit_CreateImageSubtitleRenderer(decoder, video_w, video_h, screen_w, screen_h);
            break;
        default:
            Kit_SetError("Unrecognized subtitle format for stream %d", stream_index);
            break;
    }
    if(renderer == NULL) {
        goto exit_3;
    }

    subtitle_decoder->atlas = atlas;
    subtitle_decoder->renderer = renderer;
    subtitle_decoder->output = output;
    return decoder;

exit_3:
    Kit_FreeAtlas(atlas);
exit_2:
    Kit_CloseDecoder(&decoder);
    return NULL; // Above frees the subtitle_decoder also.
exit_1:
    free(subtitle_decoder);
exit_0:
    return NULL;
}

void Kit_SetSubtitleDecoderSize(const Kit_Decoder *dec, int screen_w, int screen_h) {
    assert(dec != NULL);
    const Kit_SubtitleDecoder *subtitle_dec = dec->userdata;
    Kit_SetSubtitleRendererSize(subtitle_dec->renderer, screen_w, screen_h);
}

void Kit_GetSubtitleDecoderSDLTexture(const Kit_Decoder *dec, SDL_Texture *texture, double sync_ts) {
    assert(dec != NULL);
    assert(texture != NULL);

    const Kit_SubtitleDecoder *subtitle_dec = dec->userdata;
    Kit_GetSubtitleRendererSDLTexture(subtitle_dec->renderer, subtitle_dec->atlas, texture, sync_ts);
}

int Kit_GetSubtitleDecoderSDLTextureInfo(const Kit_Decoder *dec, SDL_Rect *sources, SDL_Rect *targets, int limit) {
    const Kit_SubtitleDecoder *subtitle_dec = dec->userdata;
    return Kit_GetAtlasItems(subtitle_dec->atlas, sources, targets, limit);
}

int Kit_GetSubtitleDecoderRawFrames(const Kit_Decoder *dec, unsigned char ***items, SDL_Rect **sources, SDL_Rect **targets, double sync_ts) {
    Kit_SubtitleDecoder *subtitle_dec = dec->userdata;
    return Kit_GetSubtitleRendererRawFrames(subtitle_dec->renderer, items, sources, targets, sync_ts);
}
