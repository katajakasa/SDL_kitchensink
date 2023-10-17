#include <assert.h>

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/video/kitvideo.h"

#define KIT_VIDEO_SYNC_THRESHOLD 0.02

enum AVPixelFormat supported_list[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUYV422,
    AV_PIX_FMT_UYVY422,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV21,
    AV_PIX_FMT_RGB24,
    AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGB555,
    AV_PIX_FMT_BGR555,
    AV_PIX_FMT_RGB565,
    AV_PIX_FMT_BGR565,
    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_RGBA,
    AV_PIX_FMT_NONE
};

typedef struct Kit_VideoDecoder {
    struct SwsContext *sws;
    AVFrame *scratch_frame;
} Kit_VideoDecoder;

typedef struct Kit_VideoPacket {
    double pts;
    AVFrame *frame;
} Kit_VideoPacket;


static Kit_VideoPacket* _CreateVideoPacket(AVFrame *frame, double pts) {
    Kit_VideoPacket *p = calloc(1, sizeof(Kit_VideoPacket));
    p->frame = frame;
    p->pts = pts;
    return p;
}

static unsigned int _FindSDLPixelFormat(enum AVPixelFormat fmt) {
    switch(fmt) {
        case AV_PIX_FMT_YUV420P:
            return SDL_PIXELFORMAT_YV12;
        case AV_PIX_FMT_YUYV422:
            return SDL_PIXELFORMAT_YUY2;
        case AV_PIX_FMT_UYVY422:
            return SDL_PIXELFORMAT_UYVY;
        case AV_PIX_FMT_NV12:
            return SDL_PIXELFORMAT_NV12;
        case AV_PIX_FMT_NV21:
            return SDL_PIXELFORMAT_NV21;
        default:
            return SDL_PIXELFORMAT_RGBA32;
    }
}

static enum AVPixelFormat _FindAVPixelFormat(unsigned int fmt) {
    switch(fmt) {
        case SDL_PIXELFORMAT_YV12: return AV_PIX_FMT_YUV420P;
        case SDL_PIXELFORMAT_YUY2: return AV_PIX_FMT_YUYV422;
        case SDL_PIXELFORMAT_UYVY: return AV_PIX_FMT_UYVY422;
        case SDL_PIXELFORMAT_NV12: return AV_PIX_FMT_NV12;
        case SDL_PIXELFORMAT_NV21: return AV_PIX_FMT_NV21;
        case SDL_PIXELFORMAT_ARGB32: return AV_PIX_FMT_ARGB;
        case SDL_PIXELFORMAT_RGBA32: return AV_PIX_FMT_RGBA;
        case SDL_PIXELFORMAT_BGR24: return AV_PIX_FMT_BGR24;
        case SDL_PIXELFORMAT_RGB24: return AV_PIX_FMT_RGB24;
        case SDL_PIXELFORMAT_RGB555: return AV_PIX_FMT_RGB555;
        case SDL_PIXELFORMAT_BGR555: return AV_PIX_FMT_BGR555;
        case SDL_PIXELFORMAT_RGB565: return AV_PIX_FMT_RGB565;
        case SDL_PIXELFORMAT_BGR565: return AV_PIX_FMT_BGR565;
        default:
            return AV_PIX_FMT_NONE;
    }
}

static struct SwsContext* _GetSwsContext(
    struct SwsContext *old_context,
    int src_w,
    int src_h,
    int dst_w,
    int dst_h,
    enum AVPixelFormat in_fmt,
    enum AVPixelFormat out_fmt
) {
    struct SwsContext* new_context = sws_getCachedContext(
        old_context,
        src_w,
        src_h,
        in_fmt,
        dst_w,
        dst_h,
        out_fmt,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );
    if(new_context == NULL) {
        Kit_SetError("Unable to initialize video converter context");
    }
    return new_context;
}

static void free_out_video_packet_cb(void *packet) {
    Kit_VideoPacket *p = packet;
    av_freep(&p->frame->data[0]);
    av_frame_free(&p->frame);
    free(p);
}

static void dec_read_video(const Kit_Decoder *dec) {
    Kit_VideoDecoder *video_dec = dec->userdata;
    AVFrame *out_frame = NULL;
    Kit_VideoPacket *out_packet = NULL;
    double pts;
    int ret = 0;

    while(!ret && Kit_CanWriteDecoderOutput(dec)) {
        ret = avcodec_receive_frame(dec->codec_ctx, video_dec->scratch_frame);
        if(!ret) {
            out_frame = av_frame_alloc();
            av_image_alloc(
                    out_frame->data,
                    out_frame->linesize,
                    dec->codec_ctx->width,
                    dec->codec_ctx->height,
                    _FindAVPixelFormat(dec->output.format),
                    1);

            // Scale from source format to target format, don't touch the size
            video_dec->sws = _GetSwsContext(
                video_dec->sws,
                video_dec->scratch_frame->width,
                video_dec->scratch_frame->height,
                video_dec->scratch_frame->width,
                video_dec->scratch_frame->height,
                dec->codec_ctx->pix_fmt,
                _FindAVPixelFormat(dec->output.format));
            sws_scale(
                video_dec->sws,
                (const unsigned char * const *)video_dec->scratch_frame->data,
                video_dec->scratch_frame->linesize,
                0,
                video_dec->scratch_frame->height,
                out_frame->data,
                out_frame->linesize);

            // Copy required props to safety
            out_frame->width = video_dec->scratch_frame->width;
            out_frame->height = video_dec->scratch_frame->height;
            out_frame->sample_aspect_ratio = video_dec->scratch_frame->sample_aspect_ratio;

            // Get presentation timestamp
            pts = video_dec->scratch_frame->best_effort_timestamp;
            pts *= av_q2d(dec->format_ctx->streams[dec->stream_index]->time_base);

            // Lock, write to audio buffer, unlock
            out_packet = _CreateVideoPacket(out_frame, pts);
            Kit_WriteDecoderOutput(dec, out_packet);
        }
    }
}

static int dec_decode_video_cb(Kit_Decoder *dec, AVPacket *in_packet) {
    assert(dec != NULL);
    assert(in_packet != NULL);

    // Try to clear the buffer first. We might have too much content in the ffmpeg buffer,
    // so we want to clear it of outgoing data if we can.
    dec_read_video(dec);

    // Write packet to the decoder for handling.
    if(avcodec_send_packet(dec->codec_ctx, in_packet) < 0) {
        return 1;
    }

    // Some input data was put in successfully, so try again to get frames.
    dec_read_video(dec);
    return 0;
}

static void dec_close_video_cb(Kit_Decoder *dec) {
    if(dec == NULL) return;

    Kit_VideoDecoder *video_dec = dec->userdata;
    if(video_dec->scratch_frame != NULL) {
        av_frame_free(&video_dec->scratch_frame);
    }
    if(video_dec->sws != NULL) {
        sws_freeContext(video_dec->sws);
    }
    free(video_dec);
}

Kit_Decoder* Kit_CreateVideoDecoder(const Kit_Source *src, int stream_index) {
    assert(src != NULL);
    if(stream_index < 0) {
        return NULL;
    }

    const Kit_LibraryState *state = Kit_GetLibraryState();

    // First the generic decoder component ...
    Kit_Decoder *dec = Kit_CreateDecoder(
        src,
        stream_index,
        state->video_buf_frames,
        free_out_video_packet_cb,
        state->thread_count);
    if(dec == NULL) {
        goto EXIT_0;
    }

    // ... then allocate the video decoder
    Kit_VideoDecoder *video_dec = calloc(1, sizeof(Kit_VideoDecoder));
    if(video_dec == NULL) {
        goto EXIT_1;
    }

    // Create temporary video frame
    video_dec->scratch_frame = av_frame_alloc();
    if(video_dec->scratch_frame == NULL) {
        Kit_SetError("Unable to initialize temporary video frame");
        goto EXIT_2;
    }

    // Find best output format for us
    enum AVPixelFormat output_format = avcodec_find_best_pix_fmt_of_list(
        supported_list, dec->codec_ctx->pix_fmt, 1, NULL);

    // Set format configs
    Kit_OutputFormat output;
    memset(&output, 0, sizeof(Kit_OutputFormat));
    output.width = dec->codec_ctx->width;
    output.height = dec->codec_ctx->height;
    output.format = _FindSDLPixelFormat(output_format);

    // Create scaler for handling format changes
    video_dec->sws = _GetSwsContext(
        video_dec->sws,
        dec->codec_ctx->width,
        dec->codec_ctx->height,
        dec->codec_ctx->width,
        dec->codec_ctx->height,
        dec->codec_ctx->pix_fmt,
        _FindAVPixelFormat(output.format)
    );
    if(video_dec->sws == NULL) {
        goto EXIT_3;
    }

    // Set callbacks and userdata, and we're go
    dec->dec_decode = dec_decode_video_cb;
    dec->dec_close = dec_close_video_cb;
    dec->userdata = video_dec;
    dec->output = output;
    return dec;

EXIT_3:
    av_frame_free(&video_dec->scratch_frame);
EXIT_2:
    free(video_dec);
EXIT_1:
    Kit_CloseDecoder(dec);
EXIT_0:
    return NULL;
}

double Kit_GetVideoDecoderPTS(const Kit_Decoder *dec) {
    const Kit_VideoPacket *packet = Kit_PeekDecoderOutput(dec);
    if(packet == NULL) {
        return -1.0;
    }
    return packet->pts;
}

int Kit_GetVideoDecoderData(Kit_Decoder *dec, SDL_Texture *texture, SDL_Rect *area) {
    assert(dec != NULL);
    assert(texture != NULL);

    Kit_VideoPacket *packet = NULL;
    double sync_ts = 0;
    unsigned int limit_rounds = 0;

    // First, peek the next packet. Make sure we have something to read.
    packet = Kit_PeekDecoderOutput(dec);
    if(packet == NULL) {
        return 0;
    }

    // If packet should not yet be played, stop here and wait.
    // If packet should have already been played, skip it and try to find a better packet.
    // For video, we *try* to return a frame, even if we are out of sync. It is better than
    // not showing anything.
    sync_ts = _GetSystemTime() - dec->clock_sync;
    if(packet->pts > sync_ts + KIT_VIDEO_SYNC_THRESHOLD) {
        return 0;
    }
    limit_rounds = Kit_GetDecoderOutputLength(dec);
    while(packet != NULL && packet->pts < sync_ts - KIT_VIDEO_SYNC_THRESHOLD && --limit_rounds) {
        Kit_AdvanceDecoderOutput(dec);
        free_out_video_packet_cb(packet);
        packet = Kit_PeekDecoderOutput(dec);
    }
    if(packet == NULL) {
        return 0;
    }

    // Update output texture with current video data.
    // Note that frame size may change on the fly. Take that into account.
    area->w = packet->frame->width;
    area->h = packet->frame->height;
    area->x = 0;
    area->y = 0;
    switch(dec->output.format) {
        case SDL_PIXELFORMAT_YV12:
        case SDL_PIXELFORMAT_IYUV:
            SDL_UpdateYUVTexture(
                texture, area,
                packet->frame->data[0], packet->frame->linesize[0],
                packet->frame->data[1], packet->frame->linesize[1],
                packet->frame->data[2], packet->frame->linesize[2]);
            break;
        default:
            SDL_UpdateTexture(
                texture, area,
                packet->frame->data[0],
                packet->frame->linesize[0]);
            break;
    }

    // Advance buffer, and free the decoded frame.
    Kit_AdvanceDecoderOutput(dec);
    dec->clock_pos = packet->pts;
    dec->aspect_ratio = packet->frame->sample_aspect_ratio;
    free_out_video_packet_cb(packet);

    return 0;
}
