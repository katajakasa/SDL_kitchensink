#include <assert.h>

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/utils/kitbuffer.h"
#include "kitchensink/internal/video/kitvideo.h"
#include "kitchensink/internal/utils/kitlog.h"

#define KIT_VIDEO_SYNC_THRESHOLD 0.01

enum AVPixelFormat supported_list[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUYV422,
    AV_PIX_FMT_UYVY422,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV12,
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
        case SDL_PIXELFORMAT_ARGB32: return AV_PIX_FMT_BGRA;
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

static void free_out_video_packet_cb(void *packet) {
    Kit_VideoPacket *p = packet;
    av_freep(&p->frame->data[0]);
    av_frame_free(&p->frame);
    free(p);
}

static void dec_decode_video_cb(Kit_Decoder *dec, AVPacket *in_packet) {
    assert(dec != NULL);
    assert(in_packet != NULL);
    
    Kit_VideoDecoder *video_dec = dec->userdata;
    int frame_finished;
    

    while(in_packet->size > 0) {
        int len = avcodec_decode_video2(dec->codec_ctx, video_dec->scratch_frame, &frame_finished, in_packet);
        if(len < 0) {
            return;
        }

        if(frame_finished) {
            // Target frame
            AVFrame *out_frame = av_frame_alloc();
            av_image_alloc(
                    out_frame->data,
                    out_frame->linesize,
                    dec->codec_ctx->width,
                    dec->codec_ctx->height,
                    _FindAVPixelFormat(dec->output.format),
                    1);

            // Scale from source format to target format, don't touch the size
            sws_scale(
                video_dec->sws,
                (const unsigned char * const *)video_dec->scratch_frame->data,
                video_dec->scratch_frame->linesize,
                0,
                dec->codec_ctx->height,
                out_frame->data,
                out_frame->linesize);

            // Get presentation timestamp
            double pts = av_frame_get_best_effort_timestamp(video_dec->scratch_frame);
            pts *= av_q2d(dec->format_ctx->streams[dec->stream_index]->time_base);

            // Lock, write to audio buffer, unlock
            Kit_VideoPacket *out_packet = _CreateVideoPacket(out_frame, pts);
            Kit_WriteDecoderOutput(dec, out_packet);
        }
        in_packet->size -= len;
        in_packet->data += len;
    }
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

    Kit_LibraryState *state = Kit_GetLibraryState();

    // First the generic decoder component ...
    Kit_Decoder *dec = Kit_CreateDecoder(
        src,
        stream_index,
        state->video_buf_frames,
        free_out_video_packet_cb,
        state->thread_count);
    if(dec == NULL) {
        goto exit_0;
    }

    // ... then allocate the video decoder
    Kit_VideoDecoder *video_dec = calloc(1, sizeof(Kit_VideoDecoder));
    if(video_dec == NULL) {
        goto exit_1;
    }

    // Create temporary video frame
    video_dec->scratch_frame = av_frame_alloc();
    if(video_dec->scratch_frame == NULL) {
        Kit_SetError("Unable to initialize temporary video frame");
        goto exit_2;
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
    video_dec->sws = sws_getContext(
        dec->codec_ctx->width, // Source w
        dec->codec_ctx->height, // Source h
        dec->codec_ctx->pix_fmt, // Source fmt
        dec->codec_ctx->width, // Target w
        dec->codec_ctx->height, // Target h
        _FindAVPixelFormat(output.format), // Target fmt
        SWS_BILINEAR,
        NULL, NULL, NULL);
    if(video_dec->sws == NULL) {
        Kit_SetError("Unable to initialize video converter context");
        goto exit_3;
    }

    // Set callbacks and userdata, and we're go
    dec->dec_decode = dec_decode_video_cb;
    dec->dec_close = dec_close_video_cb;
    dec->userdata = video_dec;
    dec->output = output;
    return dec;

exit_3:
    av_frame_free(&video_dec->scratch_frame);
exit_2:
    free(video_dec);
exit_1:
    Kit_CloseDecoder(dec);
exit_0:
    return NULL;
}

int Kit_GetVideoDecoderData(Kit_Decoder *dec, SDL_Texture *texture) {
    assert(dec != NULL);
    assert(texture != NULL);

    Kit_VideoPacket *packet = Kit_PeekDecoderOutput(dec);
    if(packet == NULL) {
        return 0;
    }

    double sync_ts = _GetSystemTime() - dec->clock_sync;

    // Check if we want the packet
    if(packet->pts > sync_ts + KIT_VIDEO_SYNC_THRESHOLD) {
        // Video is ahead, don't show yet.
        return 0;
    } else if(packet->pts < sync_ts - KIT_VIDEO_SYNC_THRESHOLD) {
        // Video is lagging, skip until we find a good PTS to continue from.
        while(packet != NULL) {
            Kit_AdvanceDecoderOutput(dec);
            free_out_video_packet_cb(packet);
            packet = Kit_PeekDecoderOutput(dec);
            if(packet == NULL) {
                break;
            } else {
                dec->clock_pos = packet->pts;
            }
            if(packet->pts > sync_ts - KIT_VIDEO_SYNC_THRESHOLD) {
                break;
            }
        }
    }

    // If we have no viable packet, just skip
    if(packet == NULL) {
        return 0;
    }

    // Update output texture with current video data.
    // Take formats into account.
    switch(dec->output.format) {
        case SDL_PIXELFORMAT_YV12:
        case SDL_PIXELFORMAT_IYUV:
            SDL_UpdateYUVTexture(
                texture, NULL, 
                packet->frame->data[0], packet->frame->linesize[0],
                packet->frame->data[1], packet->frame->linesize[1],
                packet->frame->data[2], packet->frame->linesize[2]);
            break;
        default:
            SDL_UpdateTexture(
                texture, NULL,
                packet->frame->data[0],
                packet->frame->linesize[0]);
            break;
    }

    // Advance buffer, and free the decoded frame.
    Kit_AdvanceDecoderOutput(dec);
    dec->clock_pos = packet->pts;
    free_out_video_packet_cb(packet);

    return 0;
}
