#include "kitchensink/kitplayer.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitbuffer.h"
#include "kitchensink/internal/kitringbuffer.h"
#include "kitchensink/internal/kitlist.h"
#include "kitchensink/internal/kitlibstate.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/pixfmt.h>
#include <libavutil/time.h>
#include <libavutil/samplefmt.h>
#include "libavutil/avstring.h"

#include <SDL2/SDL.h>
#include <ass/ass.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

// Threshold is in seconds
#define VIDEO_SYNC_THRESHOLD 0.01
#define AUDIO_SYNC_THRESHOLD 0.05
#define SUBTITLE_SYNC_THRESHOLD 0.05

// Buffersizes
#define KIT_VBUFFERSIZE 3
#define KIT_ABUFFERSIZE 64
#define KIT_CBUFFERSIZE 8
#define KIT_SBUFFERSIZE 48

typedef enum Kit_ControlPacketType {
    KIT_CONTROL_SEEK,
    KIT_CONTROL_FLUSH
} Kit_ControlPacketType;

typedef struct Kit_VideoPacket {
    double pts;
    AVPicture *frame;
} Kit_VideoPacket;

typedef struct Kit_AudioPacket {
    double pts;
    size_t original_size;
    Kit_RingBuffer *rb;
} Kit_AudioPacket;

typedef struct Kit_ControlPacket {
    Kit_ControlPacketType type;
    double value1;
} Kit_ControlPacket;

typedef struct Kit_SubtitlePacket {
    double pts_start;
    double pts_end;
    SDL_Rect *rect;
    SDL_Surface *surface;
    SDL_Texture *texture;
} Kit_SubtitlePacket;

static int _InitCodecs(Kit_Player *player, const Kit_Source *src) {
    assert(player != NULL);
    assert(src != NULL);

    AVCodecContext *acodec_ctx = NULL;
    AVCodecContext *vcodec_ctx = NULL;
    AVCodecContext *scodec_ctx = NULL;
    AVCodec *acodec = NULL;
    AVCodec *vcodec = NULL;
    AVCodec *scodec = NULL;
    AVFormatContext *format_ctx = (AVFormatContext *)src->format_ctx;

    // Make sure index seems correct
    if(src->astream_idx >= (int)format_ctx->nb_streams) {
        Kit_SetError("Invalid audio stream index: %d", src->astream_idx);
        goto exit_0;
    } else if(src->astream_idx >= 0) {
        // Find audio decoder
        acodec = avcodec_find_decoder(format_ctx->streams[src->astream_idx]->codec->codec_id);
        if(!acodec) {
            Kit_SetError("No suitable audio decoder found");
            goto exit_0;
        }

        // Copy the original audio codec context
        acodec_ctx = avcodec_alloc_context3(acodec);
        if(avcodec_copy_context(acodec_ctx, format_ctx->streams[src->astream_idx]->codec) != 0) {
            Kit_SetError("Unable to copy audio codec context");
            goto exit_0;
        }

        // Create an audio decoder context
        if(avcodec_open2(acodec_ctx, acodec, NULL) < 0) {
            Kit_SetError("Unable to allocate audio codec context");
            goto exit_1;
        }
    }

    // Make sure index seems correct
    if(src->vstream_idx >= (int)format_ctx->nb_streams) {
        Kit_SetError("Invalid video stream index: %d", src->vstream_idx);
        goto exit_2;
    } else if(src->vstream_idx >= 0) {
        // Find video decoder
        vcodec = avcodec_find_decoder(format_ctx->streams[src->vstream_idx]->codec->codec_id);
        if(!vcodec) {
            Kit_SetError("No suitable video decoder found");
            goto exit_2;
        }

        // Copy the original video codec context
        vcodec_ctx = avcodec_alloc_context3(vcodec);
        if(avcodec_copy_context(vcodec_ctx, format_ctx->streams[src->vstream_idx]->codec) != 0) {
            Kit_SetError("Unable to copy video codec context");
            goto exit_2;
        }

        // Create a video decoder context
        if(avcodec_open2(vcodec_ctx, vcodec, NULL) < 0) {
            Kit_SetError("Unable to allocate video codec context");
            goto exit_3;
        }
    }

    if(src->sstream_idx >= (int)format_ctx->nb_streams) {
        Kit_SetError("Invalid subtitle stream index: %d", src->sstream_idx);
        goto exit_2;
    } else if(src->sstream_idx >= 0) {
        // Find subtitle decoder
        scodec = avcodec_find_decoder(format_ctx->streams[src->sstream_idx]->codec->codec_id);
        if(!scodec) {
            Kit_SetError("No suitable subtitle decoder found");
            goto exit_4;
        }

        // Copy the original subtitle codec context
        scodec_ctx = avcodec_alloc_context3(scodec);
        if(avcodec_copy_context(scodec_ctx, format_ctx->streams[src->sstream_idx]->codec) != 0) {
            Kit_SetError("Unable to copy subtitle codec context");
            goto exit_4;
        }

        // Create a subtitle decoder context
        if(avcodec_open2(scodec_ctx, scodec, NULL) < 0) {
            Kit_SetError("Unable to allocate subtitle codec context");
            goto exit_5;
        }
    }

    player->acodec_ctx = acodec_ctx;
    player->vcodec_ctx = vcodec_ctx;
    player->scodec_ctx = scodec_ctx;
    player->src = src;
    return 0;

exit_5:
    avcodec_free_context(&scodec_ctx);
exit_4:
    avcodec_close(vcodec_ctx);
exit_3:
    avcodec_free_context(&vcodec_ctx);
exit_2:
    avcodec_close(acodec_ctx);
exit_1:
    avcodec_free_context(&acodec_ctx);
exit_0:
    return 1;
}

static int reset_libass_track(Kit_Player *player) {
    Kit_LibraryState *state = Kit_GetLibraryState();
    AVCodecContext *scodec_ctx = player->scodec_ctx;

    // Initialize libass track
    player->ass_track = ass_new_track(state->libass_handle);
    if(player->ass_track == NULL) {
        Kit_SetError("Unable to initialize libass track");
        return 1;
    }

    // Set up libass track headers (ffmpeg provides these)
    if(scodec_ctx->subtitle_header) {
        ass_process_codec_private(
            (ASS_Track*)player->ass_track,
            (char*)scodec_ctx->subtitle_header,
            scodec_ctx->subtitle_header_size);
    }
    return 0;
}

static void _FindPixelFormat(enum AVPixelFormat fmt, unsigned int *out_fmt) {
    switch(fmt) {
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

static void _FindAudioFormat(enum AVSampleFormat fmt, int *bytes, bool *is_signed, unsigned int *format) {
    switch(fmt) {
        case AV_SAMPLE_FMT_U8:
            *bytes = 1;
            *is_signed = false;
            *format = AUDIO_U8;
            break;
        case AV_SAMPLE_FMT_S16:
            *bytes = 2;
            *is_signed = true;
            *format = AUDIO_S16SYS;
            break;
        case AV_SAMPLE_FMT_S32:
            *bytes = 4;
            *is_signed = true;
            *format = AUDIO_S32SYS;
            break;
        default:
            *bytes = 2;
            *is_signed = true;
            *format = AUDIO_S16SYS;
            break;
    }
}

static enum AVPixelFormat _FindAVPixelFormat(unsigned int fmt) {
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

static enum AVSampleFormat _FindAVSampleFormat(int format) {
    switch(format) {
        case AUDIO_U8: return AV_SAMPLE_FMT_U8;
        case AUDIO_S16SYS: return AV_SAMPLE_FMT_S16;
        case AUDIO_S32SYS: return AV_SAMPLE_FMT_S32;
        default:
            return AV_SAMPLE_FMT_NONE;
    }
}

static unsigned int _FindAVChannelLayout(int channels) {
    switch(channels) {
        case 1: return AV_CH_LAYOUT_MONO;
        default: return AV_CH_LAYOUT_STEREO;
    }
}

static Kit_VideoPacket* _CreateVideoPacket(AVPicture *frame, double pts) {
    Kit_VideoPacket *p = calloc(1, sizeof(Kit_VideoPacket));
    p->frame = frame;
    p->pts = pts;
    return p;
}

static void _FreeVideoPacket(void *ptr) {
    Kit_VideoPacket *packet = ptr;
    avpicture_free(packet->frame);
    av_free(packet->frame);
    free(packet);
}

static Kit_AudioPacket* _CreateAudioPacket(const char* data, size_t len, double pts) {
    Kit_AudioPacket *p = calloc(1, sizeof(Kit_AudioPacket));
    p->rb = Kit_CreateRingBuffer(len);
    Kit_WriteRingBuffer(p->rb, data, len);
    p->pts = pts;
    return p;
}

static void _FreeAudioPacket(void *ptr) {
    Kit_AudioPacket *packet = ptr;
    Kit_DestroyRingBuffer(packet->rb);
    free(packet);
}

static Kit_ControlPacket* _CreateControlPacket(Kit_ControlPacketType type, double value1) {
    Kit_ControlPacket *p = calloc(1, sizeof(Kit_ControlPacket));
    p->type = type;
    p->value1 = value1;
    return p;
}

static void _FreeControlPacket(void *ptr) {
    Kit_ControlPacket *packet = ptr;
    free(packet);
}


static Kit_SubtitlePacket* _CreateSubtitlePacket(double pts_start, double pts_end, SDL_Rect *rect, SDL_Surface *surface) {
    Kit_SubtitlePacket *p = calloc(1, sizeof(Kit_SubtitlePacket));
    p->pts_start = pts_start;
    p->pts_end = pts_end;
    p->surface = surface;
    p->rect = rect;
    p->texture = NULL; // Cached texture
    return p;
}

static void _FreeSubtitlePacket(void *ptr) {
    Kit_SubtitlePacket *packet = ptr;
    SDL_FreeSurface(packet->surface);
    if(packet->texture) {
        SDL_DestroyTexture(packet->texture);
    }
    free(packet->rect);
    free(packet);
}

static double _GetSystemTime() {
    return (double)av_gettime() / 1000000.0;
}

static void _HandleVideoPacket(Kit_Player *player, AVPacket *packet) {
    assert(player != NULL);
    assert(packet != NULL);
    
    int frame_finished;
    AVCodecContext *vcodec_ctx = (AVCodecContext*)player->vcodec_ctx;
    AVFormatContext *fmt_ctx = (AVFormatContext *)player->src->format_ctx;
    AVPicture *iframe = (AVPicture*)player->tmp_vframe;

    avcodec_decode_video2(vcodec_ctx, (AVFrame*)player->tmp_vframe, &frame_finished, packet);

    if(frame_finished) {
        // Target frame
        AVPicture *oframe = av_malloc(sizeof(AVPicture));
        avpicture_alloc(
            oframe,
            _FindAVPixelFormat(player->vformat.format),
            vcodec_ctx->width,
            vcodec_ctx->height);

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
}

static void _HandleAudioPacket(Kit_Player *player, AVPacket *packet) {
    assert(player != NULL);
    assert(packet != NULL);

    int frame_finished;
    int len, len2;
    int dst_linesize;
    int dst_nb_samples, dst_bufsize;
    unsigned char **dst_data;
    AVCodecContext *acodec_ctx = (AVCodecContext*)player->acodec_ctx;
    AVFormatContext *fmt_ctx = (AVFormatContext *)player->src->format_ctx;
    struct SwrContext *swr = (struct SwrContext *)player->swr;
    AVFrame *aframe = (AVFrame*)player->tmp_aframe;

    if(packet->size > 0) {
        len = avcodec_decode_audio4(acodec_ctx, aframe, &frame_finished, packet);
        if(len < 0) {
            return;
        }

        if(frame_finished) {
            dst_nb_samples = av_rescale_rnd(
                aframe->nb_samples,
                player->aformat.samplerate,
                acodec_ctx->sample_rate,
                AV_ROUND_UP);

            av_samples_alloc_array_and_samples(
                &dst_data,
                &dst_linesize,
                player->aformat.channels,
                dst_nb_samples,
                _FindAVSampleFormat(player->aformat.format),
                0);

            len2 = swr_convert(
                swr,
                dst_data,
                aframe->nb_samples,
                (const unsigned char **)aframe->extended_data,
                aframe->nb_samples);

            dst_bufsize = av_samples_get_buffer_size(
                &dst_linesize,
                player->aformat.channels,
                len2,
                _FindAVSampleFormat(player->aformat.format), 1);

            // Get pts
            double pts = 0;
            if(packet->dts != AV_NOPTS_VALUE) {
                pts = av_frame_get_best_effort_timestamp(player->tmp_aframe);
                pts *= av_q2d(fmt_ctx->streams[player->src->astream_idx]->time_base);
            }

            // Just seeked, set sync clock & pos.
            if(player->seek_flag == 1) {
                player->vclock_pos = pts;
                player->clock_sync = _GetSystemTime() - pts;
                player->seek_flag = 0;
            }

            // Lock, write to audio buffer, unlock
            Kit_AudioPacket *apacket = _CreateAudioPacket((char*)dst_data[0], (size_t)dst_bufsize, pts);
            bool done = false;
            if(SDL_LockMutex(player->amutex) == 0) {
                if(Kit_WriteBuffer((Kit_Buffer*)player->abuffer, apacket) == 0) {
                    done = true;
                } 
                SDL_UnlockMutex(player->amutex);
            }

            // Couldn't write packet, free memory
            if(!done) {
                _FreeAudioPacket(apacket);
            }

            av_freep(&dst_data[0]);
            av_freep(&dst_data);
        }
    }
}

static void _HandleBitmapSubtitle(Kit_SubtitlePacket** spackets, int *n, Kit_Player *player, double pts, AVSubtitle *sub, AVSubtitleRect *rect) {
    if(rect->nb_colors == 256) {
        // Paletted image based subtitles. Convert and set palette.
        SDL_Surface *s = SDL_CreateRGBSurfaceFrom(
            rect->pict.data[0],
            rect->w, rect->h, 8,
            rect->pict.linesize[0],
            0, 0, 0, 0);

        SDL_SetPaletteColors(s->format->palette, (SDL_Color*)rect->pict.data[1], 0, 256);

        Uint32 rmask, gmask, bmask, amask;
        #if SDL_BYTEORDER == SDL_BIG_ENDIAN
            rmask = 0xff000000;
            gmask = 0x00ff0000;
            bmask = 0x0000ff00;
            amask = 0x000000ff;
        #else
            rmask = 0x000000ff;
            gmask = 0x0000ff00;
            bmask = 0x00ff0000;
            amask = 0xff000000;
        #endif
        SDL_Surface *tmp = SDL_CreateRGBSurface(
            0, rect->w, rect->h, 32,
            rmask, gmask, bmask, amask);
        SDL_BlitSurface(s, NULL, tmp, NULL);
        SDL_FreeSurface(s);

        SDL_Rect *dst_rect = malloc(sizeof(SDL_Rect));
        dst_rect->x = rect->x;
        dst_rect->y = rect->y;
        dst_rect->w = rect->w;
        dst_rect->h = rect->h;

        double start = pts + (sub->start_display_time / 1000.0f);
        double end = -1;
        if(sub->end_display_time < UINT_MAX) {
            end = pts + (sub->end_display_time / 1000.0f);
        }

        spackets[(*n)++] = _CreateSubtitlePacket(start, end, dst_rect, tmp);
    }
}

static void _ProcessAssSubtitleRect(Kit_Player *player, AVSubtitleRect *rect) {
    ass_process_data((ASS_Track*)player->ass_track, rect->ass, strlen(rect->ass));
}

#define _r(c) ((c)  >> 24)
#define _g(c) (((c) >> 16) & 0xFF)
#define _b(c) (((c) >> 8)  & 0xFF)
#define _a(c) ((c) & 0xFF)

static void _ProcessAssImage(SDL_Surface *surface, const ASS_Image *img) {
    int x, y;
    unsigned char opacity = 255 - _a(img->color);
    unsigned char r = _r(img->color);
    unsigned char g = _g(img->color);
    unsigned char b = _b(img->color);

    unsigned char *src;
    unsigned char *dst;

    src = img->bitmap;
    dst = (unsigned char*)surface->pixels;
    for(y = 0; y < img->h; y++) {
        for(x = 0; x < img->w; x++) {
            unsigned int k = ((unsigned) src[x]) * opacity / 255;
            dst[x * 3] = (k * b + (255 - k) * dst[x * 3]) / 255;
            dst[x * 3 + 1] = (k * g + (255 - k) * dst[x * 3 + 1]) / 255;
            dst[x * 3 + 2] = (k * r + (255 - k) * dst[x * 3 + 2]) / 255;
        }
        src += img->stride;
        dst += surface->pitch;
    }
}

static void _HandleAssSubtitle(Kit_SubtitlePacket** spackets, int *n, Kit_Player *player, double pts, AVSubtitle *sub) {
    double start = pts + (sub->start_display_time / 1000.0f);
    double end = pts + (sub->end_display_time / 1000.0f);

    // Process current chunk of data
    unsigned int now = start * 1000;
    int change = 0;
    ASS_Image *images = ass_render_frame((ASS_Renderer*)player->ass_renderer, (ASS_Track*)player->ass_track, now, &change);

    // Convert to SDL_Surfaces
    if(change > 0) {
        ASS_Image *now = images;
        if(now != NULL) {
            do {
                Uint32 rmask, gmask, bmask, amask;
                #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                    rmask = 0xff000000;
                    gmask = 0x00ff0000;
                    bmask = 0x0000ff00;
                    amask = 0x000000ff;
                #else
                    rmask = 0x000000ff;
                    gmask = 0x0000ff00;
                    bmask = 0x00ff0000;
                    amask = 0xff000000;
                #endif
                SDL_Surface *tmp = SDL_CreateRGBSurface(
                    0, now->w, now->h, 32,
                    rmask, gmask, bmask, amask);

                _ProcessAssImage(tmp, now);

                SDL_Rect *dst_rect = malloc(sizeof(SDL_Rect));
                dst_rect->x = now->dst_x;
                dst_rect->y = now->dst_y;
                dst_rect->w = now->w;
                dst_rect->h = now->h;

                spackets[(*n)++] = _CreateSubtitlePacket(start, end, dst_rect, tmp);
            } while((now = now->next) != NULL);
        }
    }
}

static void _HandleSubtitlePacket(Kit_Player *player, AVPacket *packet) {
    assert(player != NULL);
    assert(packet != NULL);

    int frame_finished;
    int len;
    AVCodecContext *scodec_ctx = (AVCodecContext*)player->scodec_ctx;
    AVFormatContext *fmt_ctx = (AVFormatContext *)player->src->format_ctx;
    Kit_SubtitlePacket *tmp = NULL;
    unsigned int it;
    AVSubtitle sub;
    memset(&sub, 0, sizeof(AVSubtitle));

    if(packet->size > 0) {
        len = avcodec_decode_subtitle2(scodec_ctx, &sub, &frame_finished, packet);
        if(len < 0) {
            return;
        }

        if(frame_finished) {
            // Get pts
            double pts = 0;
            if(packet->dts != AV_NOPTS_VALUE) {
                pts = packet->pts;
                pts *= av_q2d(fmt_ctx->streams[player->src->sstream_idx]->time_base);
            }

            // Convert subtitles to SDL_Surface and create a packet
            Kit_SubtitlePacket *spackets[KIT_SBUFFERSIZE];
            memset(spackets, 0, sizeof(Kit_SubtitlePacket*) * KIT_SBUFFERSIZE);

            int n = 0;
            bool has_ass = false;
            for(int r = 0; r < sub.num_rects; r++) {
                switch(sub.rects[r]->type) {
                    case SUBTITLE_BITMAP:
                        _HandleBitmapSubtitle(spackets, &n, player, pts, &sub, sub.rects[r]);
                        break;
                    case SUBTITLE_ASS:
                        _ProcessAssSubtitleRect(player, sub.rects[r]);
                        has_ass = true;
                        break;
                    case SUBTITLE_TEXT:
                        break;
                    case SUBTITLE_NONE:
                        break;
                }
            }

            // Process libass content
            if(has_ass) {
                _HandleAssSubtitle(spackets, &n, player, pts, &sub);
            }

            // Lock, write to subtitle buffer, unlock
            if(SDL_LockMutex(player->smutex) == 0) {
                if(has_ass) {
                    Kit_ClearList((Kit_List*)player->sbuffer);
                } else {
                    // Clear out old subtitles that should only be valid until next (this) subtitle
                    it = 0;
                    while((tmp = Kit_IterateList((Kit_List*)player->sbuffer, &it)) != NULL) {
                        if(tmp->pts_end < 0) {
                            Kit_RemoveFromList((Kit_List*)player->sbuffer, it);
                        }
                    }
                }

                // Add new subtitle
                for(int i = 0; i < KIT_SBUFFERSIZE; i++) {
                    Kit_SubtitlePacket *spacket = spackets[i];
                    if(spacket != NULL) {
                        if(Kit_WriteList((Kit_List*)player->sbuffer, spacket) == 0) {
                            spackets[i] = NULL;
                        }
                    }
                }

                // Unlock subtitle buffer
                SDL_UnlockMutex(player->smutex);
            }

            // Couldn't write packet, free memory
            for(int i = 0; i < KIT_SBUFFERSIZE; i++) {
                if(spackets[i] != NULL) {
                    _FreeSubtitlePacket(spackets[i]);
                }
            }
        }
    }
}

static void _HandlePacket(Kit_Player *player, AVPacket *packet) {
    // Check if this is a packet we need to handle and pass it on
    if(player->vcodec_ctx != NULL && packet->stream_index == player->src->vstream_idx) {
        _HandleVideoPacket(player, packet);
    }
    else if(player->acodec_ctx != NULL && packet->stream_index == player->src->astream_idx) {
        _HandleAudioPacket(player, packet);
    }
    else if(player->scodec_ctx != NULL && packet->stream_index == player->src->sstream_idx) {
        _HandleSubtitlePacket(player, packet);
    }
}

static void _HandleFlushCommand(Kit_Player *player, Kit_ControlPacket *packet) {
    if(player->abuffer != NULL) {
        if(SDL_LockMutex(player->amutex) == 0) {
            Kit_ClearBuffer((Kit_Buffer*)player->abuffer);
            SDL_UnlockMutex(player->amutex);
        }
    }
    if(player->vbuffer != NULL) {
        if(SDL_LockMutex(player->vmutex) == 0) {
            Kit_ClearBuffer((Kit_Buffer*)player->vbuffer);
            SDL_UnlockMutex(player->vmutex);
        }
    }
    if(player->sbuffer != NULL) {
        if(SDL_LockMutex(player->smutex) == 0) {
            Kit_ClearList((Kit_List*)player->sbuffer);
            SDL_UnlockMutex(player->smutex);
        }
    }
    reset_libass_track(player);
}

static void _HandleSeekCommand(Kit_Player *player, Kit_ControlPacket *packet) {
    AVFormatContext *fmt_ctx = (AVFormatContext *)player->src->format_ctx;

    // Find and limit absolute position
    double seek = packet->value1;
    double duration = Kit_GetPlayerDuration(player);
    if(player->vclock_pos + seek <= 0) {
        seek = -player->vclock_pos;
    }
    if(player->vclock_pos + seek >= duration) {
        seek = duration - player->vclock_pos;
    }
    double absolute_pos = player->vclock_pos + seek;
    int64_t seek_target = absolute_pos * AV_TIME_BASE;

    // Seek to timestamp.
    avformat_seek_file(fmt_ctx, -1, INT64_MIN, seek_target, INT64_MAX, 0);
    if(player->vcodec_ctx != NULL)
        avcodec_flush_buffers(player->vcodec_ctx);
    if(player->acodec_ctx != NULL)
        avcodec_flush_buffers(player->acodec_ctx);

    // On first packet, set clock and current position
    player->seek_flag = 1;
}

static void _HandleControlPacket(Kit_Player *player, Kit_ControlPacket *packet) {
    switch(packet->type) {
        case KIT_CONTROL_FLUSH:
            _HandleFlushCommand(player, packet);
            break;
        case KIT_CONTROL_SEEK:
            _HandleSeekCommand(player, packet);
            break;
    }
}

// Return 0 if stream is good but nothing else to do for now
// Return -1 if there is still work to be done
// Return 1 if there was an error or stream end
static int _UpdatePlayer(Kit_Player *player) {
    assert(player != NULL);

    AVFormatContext *format_ctx = (AVFormatContext*)player->src->format_ctx;

    // Handle control queue
    if(SDL_LockMutex(player->cmutex) == 0) {
        Kit_ControlPacket *cpacket;
        while((cpacket = (Kit_ControlPacket*)Kit_ReadBuffer(player->cbuffer)) != NULL) {
            _HandleControlPacket(player, cpacket);
            _FreeControlPacket(cpacket);
        }
        SDL_UnlockMutex(player->cmutex);
    }

    // If either buffer is full, just stop here for now.
    // Since we don't know what kind of data is going to come out of av_read_frame, we really
    // want to make sure we are prepared for everything :)
    if(player->vcodec_ctx != NULL) {
        if(SDL_LockMutex(player->vmutex) == 0) {
            int ret = Kit_IsBufferFull(player->vbuffer);
            SDL_UnlockMutex(player->vmutex);
            if(ret == 1) {
                return 0;
            }
        }
    }
    if(player->acodec_ctx != NULL) {
        if(SDL_LockMutex(player->amutex) == 0) {
            int ret = Kit_IsBufferFull(player->abuffer);
            SDL_UnlockMutex(player->amutex);
            if(ret == 1) {
                return 0;
            }
        }
    }

    // Attempt to read frame. Just return here if it fails.
    AVPacket packet;
    if(av_read_frame(format_ctx, &packet) < 0) {
        return 1;
    }
    _HandlePacket(player, &packet);
    av_free_packet(&packet);
    return -1;
}

static int _DecoderThread(void *ptr) {
    Kit_Player *player = (Kit_Player*)ptr;
    bool is_running = true;
    bool is_playing = true;
    int ret;

    while(is_running) {
        if(player->state == KIT_CLOSED) {
            is_running = false;
            continue;
        }
        if(player->state == KIT_PLAYING) {
            is_playing = true;
        }
        while(is_running && is_playing) {
            if(player->state == KIT_CLOSED) {
                is_running = false;
                continue;
            }
            if(player->state == KIT_STOPPED) {
                is_playing = false;
                continue;
            }

            // Get more data from demuxer, decode. Wait a bit if there's no more work for now.
            ret = _UpdatePlayer(player);
            if(ret == 1) {
                player->state = KIT_STOPPED;
            } else if(ret == 0) {
                SDL_Delay(1);
            }
        }

        // Just idle while waiting for work.
        SDL_Delay(10);
    }

    return 0;
}

static const char * const font_mime[] = {
    "application/x-font-ttf",
    "application/x-truetype-font",
    "application/vnd.ms-opentype",
    NULL
};

static bool attachment_is_font(AVStream *stream) {
    AVDictionaryEntry *tag = av_dict_get(stream->metadata, "mimetype", NULL, AV_DICT_MATCH_CASE);
    if(tag) {
        for(int n = 0; font_mime[n]; n++) {
            if(av_strcasecmp(font_mime[n], tag->value) == 0) {
                return true;
            }
        }
    }
    return false;
}

Kit_Player* Kit_CreatePlayer(const Kit_Source *src) {
    assert(src != NULL);

    Kit_Player *player = calloc(1, sizeof(Kit_Player));
    if(player == NULL) {
        Kit_SetError("Unable to allocate player");
        return NULL;
    }

    AVCodecContext *acodec_ctx = NULL;
    AVCodecContext *vcodec_ctx = NULL;
    AVCodecContext *scodec_ctx = NULL;

    // Initialize codecs
    if(_InitCodecs(player, src) != 0) {
        goto error;
    }

    // Init audio codec information if audio codec is initialized
    acodec_ctx = (AVCodecContext*)player->acodec_ctx;
    if(acodec_ctx != NULL) {
        player->aformat.samplerate = acodec_ctx->sample_rate;
        player->aformat.channels = acodec_ctx->channels;
        player->aformat.is_enabled = true;
        player->aformat.stream_idx = src->astream_idx;
        _FindAudioFormat(acodec_ctx->sample_fmt, &player->aformat.bytes, &player->aformat.is_signed, &player->aformat.format);

        player->swr = swr_alloc_set_opts(
            NULL,
            _FindAVChannelLayout(player->aformat.channels), // Target channel layout
            _FindAVSampleFormat(player->aformat.format), // Target fmt
            player->aformat.samplerate, // Target samplerate
            acodec_ctx->channel_layout, // Source channel layout
            acodec_ctx->sample_fmt, // Source fmt
            acodec_ctx->sample_rate, // Source samplerate
            0, NULL);
        if(swr_init((struct SwrContext *)player->swr) != 0) {
            Kit_SetError("Unable to initialize audio converter context");
            goto error;
        }

        player->abuffer = Kit_CreateBuffer(KIT_ABUFFERSIZE, _FreeAudioPacket);
        if(player->abuffer == NULL) {
            Kit_SetError("Unable to initialize audio ringbuffer");
            goto error;
        }

        player->tmp_aframe = av_frame_alloc();
        if(player->tmp_aframe == NULL) {
            Kit_SetError("Unable to initialize temporary audio frame");
            goto error;
        }
    }

    // Initialize video codec information is initialized
    vcodec_ctx = (AVCodecContext*)player->vcodec_ctx;
    if(vcodec_ctx != NULL) {
        player->vformat.is_enabled = true;
        player->vformat.width = vcodec_ctx->width;
        player->vformat.height = vcodec_ctx->height;
        player->vformat.stream_idx = src->vstream_idx;
        _FindPixelFormat(vcodec_ctx->pix_fmt, &player->vformat.format);

        player->sws = sws_getContext(
            vcodec_ctx->width, // Source w
            vcodec_ctx->height, // Source h
            vcodec_ctx->pix_fmt, // Source fmt
            vcodec_ctx->width, // Target w
            vcodec_ctx->height, // Target h
            _FindAVPixelFormat(player->vformat.format), // Target fmt
            SWS_BICUBIC,
            NULL, NULL, NULL);
        if((struct SwsContext *)player->sws == NULL) {
            Kit_SetError("Unable to initialize video converter context");
            goto error;
        }

        player->vbuffer = Kit_CreateBuffer(KIT_VBUFFERSIZE, _FreeVideoPacket);
        if(player->vbuffer == NULL) {
            Kit_SetError("Unable to initialize video ringbuffer");
            goto error;
        }

        player->tmp_vframe = av_frame_alloc();
        if(player->tmp_vframe == NULL) {
            Kit_SetError("Unable to initialize temporary video frame");
            goto error;
        }
    }

    // Initialize subtitle codec
    scodec_ctx = (AVCodecContext*)player->scodec_ctx;
    if(scodec_ctx != NULL) {
        player->sformat.is_enabled = true;
        player->sformat.stream_idx = src->sstream_idx;

        // subtitle packet buffer
        player->sbuffer = Kit_CreateList(KIT_SBUFFERSIZE, _FreeSubtitlePacket);
        if(player->sbuffer == NULL) {
            Kit_SetError("Unable to initialize active subtitle list");
            goto error;
        }

        // Initialize libass renderer
        Kit_LibraryState *state = Kit_GetLibraryState();
        player->ass_renderer = ass_renderer_init(state->libass_handle);
        if(player->ass_renderer == NULL) {
            Kit_SetError("Unable to initialize libass renderer");
            goto error;
        }

        // Read fonts from attachment streams and give them to libass
        AVFormatContext *format_ctx = player->src->format_ctx;
        for (int j = 0; j < format_ctx->nb_streams; j++) {
            AVStream *st = format_ctx->streams[j];
            if(st->codec->codec_type == AVMEDIA_TYPE_ATTACHMENT && attachment_is_font(st)) {
                const AVDictionaryEntry *tag = av_dict_get(
                    st->metadata,
                    "filename",
                    NULL,
                    AV_DICT_MATCH_CASE);
                if(tag) {
                    ass_add_font(
                        state->libass_handle,
                        tag->value, 
                        (char*)st->codec->extradata,
                        st->codec->extradata_size);
                }
            }
        }

        // Init libass fonts and window frame size
        ass_set_fonts(player->ass_renderer, NULL, NULL, 1, NULL, 1);
        ass_set_frame_size(player->ass_renderer, vcodec_ctx->width, vcodec_ctx->height);
        ass_set_font_scale(player->ass_renderer, 1.1f);
        reset_libass_track(player);
    }

    player->cbuffer = Kit_CreateBuffer(KIT_CBUFFERSIZE, _FreeControlPacket);
    if(player->cbuffer == NULL) {
        Kit_SetError("Unable to initialize control ringbuffer");
        goto error;
    }

    player->vmutex = SDL_CreateMutex();
    if(player->vmutex == NULL) {
        Kit_SetError("Unable to allocate video mutex");
        goto error;
    }

    player->amutex = SDL_CreateMutex();
    if(player->amutex == NULL) {
        Kit_SetError("Unable to allocate audio mutex");
        goto error;
    }

    player->cmutex = SDL_CreateMutex();
    if(player->cmutex == NULL) {
        Kit_SetError("Unable to allocate control buffer mutex");
        goto error;
    }

    player->smutex = SDL_CreateMutex();
    if(player->smutex == NULL) {
        Kit_SetError("Unable to allocate subtitle buffer mutex");
        goto error;
    }

    player->dec_thread = SDL_CreateThread(_DecoderThread, "Kit Decoder Thread", player);
    if(player->dec_thread == NULL) {
        Kit_SetError("Unable to create a decoder thread: %s", SDL_GetError());
        goto error;
    }

    return player;

error:
    if(player->amutex != NULL) {
        SDL_DestroyMutex(player->amutex);
    }
    if(player->vmutex != NULL) {
        SDL_DestroyMutex(player->vmutex);
    }
    if(player->cmutex != NULL) {
        SDL_DestroyMutex(player->cmutex);
    }
    if(player->smutex != NULL) {
        SDL_DestroyMutex(player->smutex);
    }
    if(player->tmp_aframe != NULL) {
        av_frame_free((AVFrame**)&player->tmp_aframe);
    }
    if(player->tmp_vframe != NULL) {
        av_frame_free((AVFrame**)&player->tmp_vframe);
    }

    Kit_DestroyBuffer((Kit_Buffer*)player->vbuffer);
    Kit_DestroyBuffer((Kit_Buffer*)player->abuffer);
    Kit_DestroyBuffer((Kit_Buffer*)player->cbuffer);
    Kit_DestroyList((Kit_List*)player->sbuffer);

    if(player->sws != NULL) {
        sws_freeContext((struct SwsContext *)player->sws);
    }
    if(player->swr != NULL) {
        swr_free((struct SwrContext **)player->swr);
    }

    if(player->ass_track != NULL) {
        ass_free_track((ASS_Track*)player->ass_track);
    }
    if(player->ass_renderer != NULL) {
        ass_renderer_done((ASS_Renderer *)player->ass_renderer);
    }
    if(player != NULL) {
        free(player);
    }
    return NULL;
}

void Kit_ClosePlayer(Kit_Player *player) {
    if(player == NULL) return;

    // Kill the decoder thread
    player->state = KIT_CLOSED;
    SDL_WaitThread(player->dec_thread, NULL);
    SDL_DestroyMutex(player->vmutex);
    SDL_DestroyMutex(player->amutex);
    SDL_DestroyMutex(player->cmutex);
    SDL_DestroyMutex(player->smutex);

    // Free up converters
    if(player->sws != NULL) {
        sws_freeContext((struct SwsContext *)player->sws);
    }
    if(player->swr != NULL) {
        swr_free((struct SwrContext **)&player->swr);
    }

    // Free temporary frames
    if(player->tmp_vframe != NULL) {
        av_frame_free((AVFrame**)&player->tmp_vframe);
    }
    if(player->tmp_aframe != NULL) {
        av_frame_free((AVFrame**)&player->tmp_aframe);
    }

    // Free contexts
    avcodec_close((AVCodecContext*)player->acodec_ctx);
    avcodec_close((AVCodecContext*)player->vcodec_ctx);
    avcodec_close((AVCodecContext*)player->scodec_ctx);
    avcodec_free_context((AVCodecContext**)&player->acodec_ctx);
    avcodec_free_context((AVCodecContext**)&player->vcodec_ctx);
    avcodec_free_context((AVCodecContext**)&player->scodec_ctx);

    // Free local audio buffers
    Kit_DestroyBuffer((Kit_Buffer*)player->cbuffer);
    Kit_DestroyBuffer((Kit_Buffer*)player->abuffer);
    Kit_DestroyBuffer((Kit_Buffer*)player->vbuffer);
    Kit_DestroyList((Kit_List*)player->sbuffer);

    // Free libass context
    if(player->ass_track != NULL) {
        ass_free_track((ASS_Track*)player->ass_track);
    }
    if(player->ass_renderer != NULL) {
        ass_renderer_done((ASS_Renderer *)player->ass_renderer);
    }

    // Free the player structure itself
    free(player);
}

int Kit_GetVideoData(Kit_Player *player, SDL_Texture *texture) {
    assert(player != NULL);

    if(player->src->vstream_idx == -1) {
        return 0;
    }

    assert(texture != NULL);

    // If paused or stopped, do nothing
    if(player->state == KIT_PAUSED) {
        return 0;
    }
    if(player->state == KIT_STOPPED) {
        return 0;
    }

    // Read a packet from buffer, if one exists. Stop here if not.
    Kit_VideoPacket *packet = NULL;
    Kit_VideoPacket *n_packet = NULL;
    if(SDL_LockMutex(player->vmutex) == 0) {
        packet = (Kit_VideoPacket*)Kit_PeekBuffer((Kit_Buffer*)player->vbuffer);
        if(packet == NULL) {
            SDL_UnlockMutex(player->vmutex);
            return 0;
        }

        // Print some data
        double cur_video_ts = _GetSystemTime() - player->clock_sync;

        // Check if we want the packet
        if(packet->pts > cur_video_ts + VIDEO_SYNC_THRESHOLD) {
            // Video is ahead, don't show yet.
            SDL_UnlockMutex(player->vmutex);
            return 0;
        } else if(packet->pts < cur_video_ts - VIDEO_SYNC_THRESHOLD) {
            // Video is lagging, skip until we find a good PTS to continue from.
            while(packet != NULL) {
                Kit_AdvanceBuffer((Kit_Buffer*)player->vbuffer);
                n_packet = (Kit_VideoPacket*)Kit_PeekBuffer((Kit_Buffer*)player->vbuffer);
                if(n_packet == NULL) {
                    break;
                }
                _FreeVideoPacket(packet);
                packet = n_packet;
                if(packet->pts > cur_video_ts - VIDEO_SYNC_THRESHOLD) {
                    break;
                }
            }
        }

        // Advance buffer one frame forwards
        Kit_AdvanceBuffer((Kit_Buffer*)player->vbuffer);
        player->vclock_pos = packet->pts;

        // Update textures as required. Handle UYV frames separately.
        if(player->vformat.format == SDL_PIXELFORMAT_YV12
            || player->vformat.format == SDL_PIXELFORMAT_IYUV)
        {
            SDL_UpdateYUVTexture(
                texture, NULL, 
                packet->frame->data[0], packet->frame->linesize[0],
                packet->frame->data[1], packet->frame->linesize[1],
                packet->frame->data[2], packet->frame->linesize[2]);
        } 
        else {
            SDL_UpdateTexture(
                texture, NULL,
                packet->frame->data[0],
                packet->frame->linesize[0]);
        }

        _FreeVideoPacket(packet);
        SDL_UnlockMutex(player->vmutex);
    } else {
        Kit_SetError("Unable to lock video buffer mutex");
        return 1;
    }

    return 0;
}

int Kit_GetSubtitleData(Kit_Player *player, SDL_Renderer *renderer) {
    assert(player != NULL);

    // If there is no audio stream, don't bother.
    if(player->src->sstream_idx == -1) {
        return 0;
    }

    assert(renderer != NULL);

    // If paused or stopped, do nothing
    if(player->state == KIT_PAUSED) {
        return 0;
    }
    if(player->state == KIT_STOPPED) {
        return 0;
    }

    unsigned int it;
    Kit_SubtitlePacket *packet = NULL;

    // Current sync timestamp
    double cur_subtitle_ts = _GetSystemTime() - player->clock_sync;

    // Read a packet from buffer, if one exists. Stop here if not.
    if(SDL_LockMutex(player->smutex) == 0) {
        // Check if refresh is required and remove old subtitles
        it = 0;
        while((packet = Kit_IterateList((Kit_List*)player->sbuffer, &it)) != NULL) {
            if(packet->pts_end >= 0 && packet->pts_end < cur_subtitle_ts) {
                Kit_RemoveFromList((Kit_List*)player->sbuffer, it);
            }
        }

        // Render subtitle bitmaps
        it = 0;
        while((packet = Kit_IterateList((Kit_List*)player->sbuffer, &it)) != NULL) {
            if(packet->texture == NULL) {
                packet->texture = SDL_CreateTextureFromSurface(renderer, packet->surface);
                SDL_SetTextureBlendMode(packet->texture, SDL_BLENDMODE_BLEND);
            }
            SDL_RenderCopy(renderer, packet->texture, NULL, packet->rect);
        }

        // Unlock subtitle buffer mutex.
        SDL_UnlockMutex(player->smutex);
    } else {
        Kit_SetError("Unable to lock subtitle buffer mutex");
        return 1;
    }

    return 0;
}

int Kit_GetAudioData(Kit_Player *player, unsigned char *buffer, int length, int cur_buf_len) {
    assert(player != NULL);

    // If there is no audio stream, don't bother.
    if(player->src->astream_idx == -1) {
        return 0;
    }

    // If asked for nothing, don't return anything either :P
    if(length == 0) {
        return 0;
    }

    assert(buffer != NULL);

    // If paused or stopped, do nothing
    if(player->state == KIT_PAUSED) {
        return 0;
    }
    if(player->state == KIT_STOPPED) {
        return 0;
    }

    // Read a packet from buffer, if one exists. Stop here if not.
    int ret = 0;
    Kit_AudioPacket *packet = NULL;
    Kit_AudioPacket *n_packet = NULL;
    if(SDL_LockMutex(player->amutex) == 0) {
        packet = (Kit_AudioPacket*)Kit_PeekBuffer((Kit_Buffer*)player->abuffer);
        if(packet == NULL) {
            SDL_UnlockMutex(player->amutex);
            return 0;
        }

        int bytes_per_sample = player->aformat.bytes * player->aformat.channels;
        double bps = bytes_per_sample * player->aformat.samplerate;
        double cur_audio_ts = _GetSystemTime() - player->clock_sync + ((double)cur_buf_len / bps);
        double diff = cur_audio_ts - packet->pts;
        int diff_samples = fabs(diff) * player->aformat.samplerate;
        
        if(packet->pts > cur_audio_ts + AUDIO_SYNC_THRESHOLD) {
            // Audio is ahead, fill buffer with some silence
            int max_diff_samples = length / bytes_per_sample;
            int max_samples = (max_diff_samples < diff_samples) ? max_diff_samples : diff_samples;

            av_samples_set_silence(
                &buffer,
                0, // Offset
                max_samples,
                player->aformat.channels,
                _FindAVSampleFormat(player->aformat.format));

            int diff_bytes = max_samples * bytes_per_sample;

            SDL_UnlockMutex(player->amutex);
            return diff_bytes;

        } else if(packet->pts < cur_audio_ts - AUDIO_SYNC_THRESHOLD) {
            // Audio is lagging, skip until good pts is found

            while(1) {
                Kit_AdvanceBuffer((Kit_Buffer*)player->abuffer);
                n_packet = (Kit_AudioPacket*)Kit_PeekBuffer((Kit_Buffer*)player->abuffer);
                if(n_packet != NULL) {
                    packet = n_packet;
                } else {
                    break;
                }
                if(packet->pts > cur_audio_ts - AUDIO_SYNC_THRESHOLD) {
                    break;
                }
            }
        }

        if(length > 0) {
            ret += Kit_ReadRingBuffer(packet->rb, (char*)buffer, length);
        }

        if(Kit_GetRingBufferLength(packet->rb) == 0) {
            Kit_AdvanceBuffer((Kit_Buffer*)player->abuffer);
            _FreeAudioPacket(packet);
        } else {
            double adjust = (double)ret / bps;
            packet->pts += adjust;
        }

        SDL_UnlockMutex(player->amutex);
    } else {
        Kit_SetError("Unable to lock audio buffer mutex");
        ret = 1;
    }

    return ret;
}

void Kit_GetPlayerInfo(const Kit_Player *player, Kit_PlayerInfo *info) {
    assert(player != NULL);
    assert(info != NULL);

    AVCodecContext *acodec_ctx = (AVCodecContext*)player->acodec_ctx;
    AVCodecContext *vcodec_ctx = (AVCodecContext*)player->vcodec_ctx;
    AVCodecContext *scodec_ctx = (AVCodecContext*)player->scodec_ctx;

    // Reset everything to 0. We might not fill all fields.
    memset(info, 0, sizeof(Kit_PlayerInfo));

    if(acodec_ctx != NULL) {
        strncpy(info->acodec, acodec_ctx->codec->name, KIT_CODECMAX-1);
        strncpy(info->acodec_name, acodec_ctx->codec->long_name, KIT_CODECNAMEMAX-1);
        memcpy(&info->audio, &player->aformat, sizeof(Kit_AudioFormat));
    }
    if(vcodec_ctx != NULL) {
        strncpy(info->vcodec, vcodec_ctx->codec->name, KIT_CODECMAX-1);
        strncpy(info->vcodec_name, vcodec_ctx->codec->long_name, KIT_CODECNAMEMAX-1);
        memcpy(&info->video, &player->vformat, sizeof(Kit_VideoFormat));
    }
    if(scodec_ctx != NULL) {
        strncpy(info->scodec, scodec_ctx->codec->name, KIT_CODECMAX-1);
        strncpy(info->scodec_name, scodec_ctx->codec->long_name, KIT_CODECNAMEMAX-1);
        memcpy(&info->subtitle, &player->sformat, sizeof(Kit_SubtitleFormat));
    }
}

Kit_PlayerState Kit_GetPlayerState(const Kit_Player *player) {
    assert(player != NULL);

    return player->state;
}

void Kit_PlayerPlay(Kit_Player *player) {
    assert(player != NULL);

    if(player->state == KIT_PLAYING) {
        return;
    }
    if(player->state == KIT_STOPPED) {
        player->clock_sync = _GetSystemTime();
    }
    if(player->state == KIT_PAUSED) {
        player->clock_sync += _GetSystemTime() - player->pause_start;
    }
    player->state = KIT_PLAYING;
}

void Kit_PlayerStop(Kit_Player *player) {
    assert(player != NULL);

    if(player->state == KIT_STOPPED) {
        return;
    }
    player->state = KIT_STOPPED;
}

void Kit_PlayerPause(Kit_Player *player) {
    assert(player != NULL);

    if(player->state != KIT_PLAYING) {
        return;
    }
    player->pause_start = _GetSystemTime();
    player->state = KIT_PAUSED;
}

int Kit_PlayerSeek(Kit_Player *player, double m_time) {
    assert(player != NULL);

    // Send packets to control stream
    if(SDL_LockMutex(player->cmutex) == 0) {
        // Flush audio and video buffers, then set seek, then unlock control queue mutex.
        Kit_WriteBuffer((Kit_Buffer*)player->cbuffer, _CreateControlPacket(KIT_CONTROL_FLUSH, 0));
        Kit_WriteBuffer((Kit_Buffer*)player->cbuffer, _CreateControlPacket(KIT_CONTROL_SEEK, m_time));
        SDL_UnlockMutex(player->cmutex);
    } else {
        Kit_SetError("Unable to lock control queue mutex");
        return 1;
    }

    return 0;
}

double Kit_GetPlayerDuration(const Kit_Player *player) {
    assert(player != NULL);

    AVFormatContext *fmt_ctx = (AVFormatContext *)player->src->format_ctx;
    return (fmt_ctx->duration / AV_TIME_BASE);
}

double Kit_GetPlayerPosition(const Kit_Player *player) {
    assert(player != NULL);

    return player->vclock_pos;
}
