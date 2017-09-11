#include <assert.h>

#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "kitchensink/internal/kitvideo.h"
#include "kitchensink/internal/kithelpers.h"
#include "kitchensink/internal/kitbuffer.h"


Kit_VideoPacket* _CreateVideoPacket(AVFrame *frame, double pts) {
    Kit_VideoPacket *p = calloc(1, sizeof(Kit_VideoPacket));
    p->frame = frame;
    p->pts = pts;
    return p;
}

void _FreeVideoPacket(void *ptr) {
    Kit_VideoPacket *packet = ptr;
    av_freep(&packet->frame->data[0]);
    av_frame_free(&packet->frame);
    free(packet);
}

void _FindPixelFormat(enum AVPixelFormat fmt, unsigned int *out_fmt) {
    switch(fmt) {
        case AV_PIX_FMT_YUV420P9:
        case AV_PIX_FMT_YUV420P10:
        case AV_PIX_FMT_YUV420P12:
        case AV_PIX_FMT_YUV420P14:
        case AV_PIX_FMT_YUV420P16:
        case AV_PIX_FMT_YUV420P:
            *out_fmt = SDL_PIXELFORMAT_YV12;
            break;
        case AV_PIX_FMT_YUYV422:
            *out_fmt = SDL_PIXELFORMAT_YUY2;
            break;
        case AV_PIX_FMT_UYVY422:
            *out_fmt = SDL_PIXELFORMAT_UYVY;
            break;
        default:
            *out_fmt = SDL_PIXELFORMAT_ABGR8888;
            break;
    }
}

enum AVPixelFormat _FindAVPixelFormat(unsigned int fmt) {
    switch(fmt) {
        case SDL_PIXELFORMAT_IYUV: return AV_PIX_FMT_YUV420P;
        case SDL_PIXELFORMAT_YV12: return AV_PIX_FMT_YUV420P;
        case SDL_PIXELFORMAT_YUY2: return AV_PIX_FMT_YUYV422;
        case SDL_PIXELFORMAT_UYVY: return AV_PIX_FMT_UYVY422;
        case SDL_PIXELFORMAT_ARGB8888: return AV_PIX_FMT_BGRA;
        case SDL_PIXELFORMAT_ABGR8888: return AV_PIX_FMT_RGBA;
        default:
            return AV_PIX_FMT_NONE;
    }
}

void _HandleVideoPacket(Kit_Player *player, AVPacket *packet) {
    assert(player != NULL);
    assert(packet != NULL);
    
    int frame_finished;
    AVCodecContext *vcodec_ctx = (AVCodecContext*)player->vcodec_ctx;
    AVFormatContext *fmt_ctx = (AVFormatContext *)player->src->format_ctx;
    AVFrame *iframe = player->tmp_vframe;

    while(packet->size > 0) {
        int len = avcodec_decode_video2(vcodec_ctx, player->tmp_vframe, &frame_finished, packet);
        if(len < 0) {
            return;
        }

        if(frame_finished) {
            // Target frame
            AVFrame *oframe = av_frame_alloc();
            av_image_alloc(
                oframe->data,
                oframe->linesize,
                vcodec_ctx->width,
                vcodec_ctx->height,
                _FindAVPixelFormat(player->vformat.format),
                1);

            // Scale from source format to target format, don't touch the size
            sws_scale(
                (struct SwsContext *)player->sws,
                (const unsigned char * const *)iframe->data,
                iframe->linesize,
                0,
                vcodec_ctx->height,
                oframe->data,
                oframe->linesize);

            // Get pts
            double pts = 0;
            if(packet->dts != AV_NOPTS_VALUE) {
                pts = av_frame_get_best_effort_timestamp(player->tmp_vframe);
                pts *= av_q2d(fmt_ctx->streams[player->src->vstream_idx]->time_base);
            }

            // Just seeked, set sync clock & pos.
            if(player->seek_flag == 1) {
                player->vclock_pos = pts;
                player->clock_sync = _GetSystemTime() - pts;
                player->seek_flag = 0;
            }

            // Lock, write to audio buffer, unlock
            Kit_VideoPacket *vpacket = _CreateVideoPacket(oframe, pts);
            bool done = false;
            if(SDL_LockMutex(player->vmutex) == 0) {
                if(Kit_WriteBuffer((Kit_Buffer*)player->vbuffer, vpacket) == 0) {
                    done = true;
                }
                SDL_UnlockMutex(player->vmutex);
            }

            // Unable to write packet, free it.
            if(!done) {
                _FreeVideoPacket(vpacket);
            }
        }
        packet->size -= len;
        packet->data += len;
    }
}
