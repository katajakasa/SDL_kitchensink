#include "kitchensink/kitplayer.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitbuffer.h"
#include "kitchensink/internal/kitringbuffer.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/pixfmt.h>
#include <libavutil/time.h>

#include <SDL2/SDL.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Threshold is in seconds
#define VIDEO_SYNC_THRESHOLD 0.01

// Buffersizes
#define KIT_VBUFFERSIZE 5
#define KIT_ABUFFERSIZE (256*1024)
#define KIT_ABUFFERLIMIT (192*1024)

typedef struct Kit_VideoPacket {
    double pts;
    AVPicture *frame;
} Kit_VideoPacket;

static int _InitCodecs(Kit_Player *player, const Kit_Source *src) {
    assert(player != NULL);
    assert(src != NULL);

    AVCodecContext *acodec_ctx = NULL;
    AVCodecContext *vcodec_ctx = NULL;
    AVCodec *acodec = NULL;
    AVCodec *vcodec = NULL;
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

    player->acodec_ctx = acodec_ctx;
    player->vcodec_ctx = vcodec_ctx;
    player->src = src;
    return 0;

exit_3:
    avcodec_free_context(&vcodec_ctx);
exit_2:
    avcodec_close(acodec_ctx);
exit_1:
    avcodec_free_context(&acodec_ctx);
exit_0:
    return 1;
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

static void _FindAudioFormat(enum AVSampleFormat fmt, int *bytes, bool *is_signed) {
    switch(fmt) {
        case AV_SAMPLE_FMT_U8:
            *bytes = 1;
            *is_signed = false;
            break;
        case AV_SAMPLE_FMT_S16:
            *bytes = 2;
            *is_signed = true;
            break;
        case AV_SAMPLE_FMT_S32:
            *bytes = 4;
            *is_signed = true;
            break;
        default:
            *bytes = 2;
            *is_signed = true;
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

static enum AVSampleFormat _FindAVSampleFormat(int bytes) {
    switch(bytes) {
        case 1: return AV_SAMPLE_FMT_U8;
        case 2: return AV_SAMPLE_FMT_S16;
        case 3: return AV_SAMPLE_FMT_S32;
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

static void _FreeVideoPacket(Kit_VideoPacket *packet) {
    avpicture_free(packet->frame);
    av_free(packet->frame);
    free(packet);
}

static Kit_VideoPacket* _CreateVideoPacket(AVPicture *frame, double pts) {
    Kit_VideoPacket *p = calloc(1, sizeof(Kit_VideoPacket));
    p->frame = frame;
    p->pts = pts;
    return p;
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

        // Lock, write to audio buffer, unlock
        Kit_VideoPacket *vpacket = _CreateVideoPacket(oframe, pts);
        if(SDL_LockMutex(player->vmutex) == 0) {
            Kit_WriteBuffer((Kit_Buffer*)player->vbuffer, vpacket);
            SDL_UnlockMutex(player->vmutex);
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
                _FindAVSampleFormat(player->aformat.bytes),
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
                _FindAVSampleFormat(player->aformat.bytes), 1);

            // Lock, write to audio buffer, unlock
            if(SDL_LockMutex(player->amutex) == 0) {
                Kit_WriteRingBuffer((Kit_RingBuffer*)player->abuffer, (char*)dst_data[0], dst_bufsize);
                SDL_UnlockMutex(player->amutex);
            }

            av_freep(&dst_data[0]);
            av_freep(&dst_data);
        }
    }
}

// Return 0 if stream is good but nothing else to do for now
// Return -1 if there is still work to be done
// Return 1 if there was an error or stream end
static int _UpdatePlayer(Kit_Player *player) {
    assert(player != NULL);

    int ret;

    // If either buffer is full, just stop here for now.
    if(player->vcodec_ctx != NULL && SDL_LockMutex(player->vmutex) == 0) {
        ret = Kit_IsBufferFull(player->vbuffer);
        SDL_UnlockMutex(player->vmutex);
        if(ret) {
            return 0;
        }
    }
    if(player->acodec_ctx != NULL && SDL_LockMutex(player->amutex) == 0) {
        ret = Kit_GetRingBufferFree(player->abuffer);
        SDL_UnlockMutex(player->amutex);
        if(ret < KIT_ABUFFERLIMIT) {
            return 0;
        }
    }

    AVPacket packet;
    AVFormatContext *format_ctx = (AVFormatContext*)player->src->format_ctx;

    // Attempt to read frame. Just return here if it fails.
    if(av_read_frame(format_ctx, &packet) < 0) {
        return 1;
    }

    // Check if this is a packet we need to handle and pass it on
    if(player->vcodec_ctx != NULL && packet.stream_index == player->src->vstream_idx) {
        _HandleVideoPacket(player, &packet);
    }
    if(player->acodec_ctx != NULL && packet.stream_index == player->src->astream_idx) {
        _HandleAudioPacket(player, &packet);
    }

    // Free packet and that's that
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

            // Get more data from demuxer, decode.
            ret = _UpdatePlayer(player);
            if(ret == 1) {
                player->state = KIT_STOPPED;
            } else if(ret == 0) {
                SDL_Delay(1);
            }
        }
        SDL_Delay(5);
    }

    return 0;
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
        _FindAudioFormat(acodec_ctx->sample_fmt, &player->aformat.bytes, &player->aformat.is_signed);

        player->swr = swr_alloc_set_opts(
            NULL,
            _FindAVChannelLayout(player->aformat.channels), // Target channel layout
            _FindAVSampleFormat(player->aformat.bytes), // Target fmt
            player->aformat.samplerate, // Target samplerate
            acodec_ctx->channel_layout, // Source channel layout
            acodec_ctx->sample_fmt, // Source fmt
            acodec_ctx->sample_rate, // Source samplerate
            0, NULL);
        if(swr_init((struct SwrContext *)player->swr) != 0) {
            Kit_SetError("Unable to initialize audio converter context");
            goto error;
        }

        player->abuffer = Kit_CreateRingBuffer(KIT_ABUFFERSIZE);
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

        player->vbuffer = Kit_CreateBuffer(KIT_VBUFFERSIZE);
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
    if(player->tmp_aframe != NULL) {
        av_frame_free((AVFrame**)&player->tmp_aframe);
    }
    if(player->tmp_vframe != NULL) {
        av_frame_free((AVFrame**)&player->tmp_vframe);
    }
    if(player->vbuffer != NULL) {
        Kit_DestroyBuffer((Kit_Buffer*)player->vbuffer);
    }
    if(player->abuffer != NULL) {
        Kit_DestroyRingBuffer((Kit_RingBuffer*)player->abuffer);
    }
    if(player->sws != NULL) {
        sws_freeContext((struct SwsContext *)player->sws);
    }
    if(player->swr != NULL) {
        swr_free((struct SwrContext **)player->swr);
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

    // Free up the ffmpeg context
    sws_freeContext((struct SwsContext *)player->sws);
    swr_free((struct SwrContext **)&player->swr);
    av_frame_free((AVFrame**)&player->tmp_vframe);
    av_frame_free((AVFrame**)&player->tmp_aframe);
    avcodec_close((AVCodecContext*)player->acodec_ctx);
    avcodec_close((AVCodecContext*)player->vcodec_ctx);
    avcodec_free_context((AVCodecContext**)&player->acodec_ctx);
    avcodec_free_context((AVCodecContext**)&player->vcodec_ctx);

    // Free local buffers
    Kit_DestroyRingBuffer((Kit_RingBuffer*)player->abuffer);
    Kit_VideoPacket *p;
    while((p = Kit_ReadBuffer(player->vbuffer)) != NULL) {
        _FreeVideoPacket(p);
    }
    Kit_DestroyBuffer((Kit_Buffer*)player->vbuffer);

    // Free the player structure itself
    free(player);
}

int Kit_RefreshTexture(Kit_Player *player, SDL_Texture *texture) {
    assert(player != NULL);
    assert(texture != NULL);

    // If paused or stopped, do nothing
    if(player->state == KIT_PAUSED) {
        return 0;
    }
    if(player->state == KIT_STOPPED) {
        return 0;
    }

    // Get texture information
    unsigned int format;
    int access, w, h;
    SDL_QueryTexture(texture, &format, &access, &w, &h);

    // Make sure the target texture looks correct
    if(w != player->vformat.width || h != player->vformat.height) {
        Kit_SetError("Incorrect target texture size: Is %dx%d, should be %dx%d",
            w, h, player->vformat.width, player->vformat.height);
        return 1;
    }

    // Make sure the texture format seems correct
    if(format != player->vformat.format) {
        Kit_SetError("Incorrect texture format");
        return 1;
    }

    // Read a packet from buffer, if one exists. Stop here if not.
    Kit_VideoPacket *packet = NULL;
    Kit_VideoPacket *n_packet = NULL;
    if(SDL_LockMutex(player->vmutex) == 0) {
        packet = (Kit_VideoPacket*)Kit_PeekBuffer((Kit_Buffer*)player->vbuffer);
        SDL_UnlockMutex(player->vmutex);
    } else {
        return 1;
    }
    if(packet == NULL) {
        return 0;
    }

    // Print some data
    double cur_video_ts = _GetSystemTime() - player->clock_sync;
    fprintf(stderr, "clock %3.3f, pts %3.3f, diff %3.3f => ",
        cur_video_ts,
        packet->pts,
        cur_video_ts - packet->pts);

    // Check if we want the packet
    if(packet->pts > cur_video_ts + VIDEO_SYNC_THRESHOLD) {
        fprintf(stderr, "WAIT\n");
        fflush(stderr);
        return 0;
    } else if(packet->pts < cur_video_ts - VIDEO_SYNC_THRESHOLD) {
        fprintf(stderr, "SKIP\n");
        fflush(stderr);
        if(SDL_LockMutex(player->vmutex) == 0) {
            while(packet != NULL) {
                Kit_AdvanceBuffer((Kit_Buffer*)player->vbuffer);
                n_packet = (Kit_VideoPacket*)Kit_PeekBuffer((Kit_Buffer*)player->vbuffer);
                if(n_packet == NULL) {
                    break;
                }
                _FreeVideoPacket(packet);
                packet = n_packet;
                cur_video_ts = _GetSystemTime() - player->clock_sync;
                player->clock_sync += cur_video_ts - packet->pts;
                if(packet->pts > cur_video_ts - VIDEO_SYNC_THRESHOLD) {
                    break;
                }
            }
            Kit_AdvanceBuffer((Kit_Buffer*)player->vbuffer);
            SDL_UnlockMutex(player->vmutex);
        }
    } else {
        fprintf(stderr, "SYNC\n");
        fflush(stderr);
        if(SDL_LockMutex(player->vmutex) == 0) {
            Kit_AdvanceBuffer((Kit_Buffer*)player->vbuffer);
            SDL_UnlockMutex(player->vmutex);
        }
    }

    // Update textures as required
    if(format == SDL_PIXELFORMAT_YV12 || format == SDL_PIXELFORMAT_IYUV) {
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

    // Free the packet
    _FreeVideoPacket(packet);
    return 0;
}

int Kit_GetAudioData(Kit_Player *player, unsigned char *buffer, size_t length) {
    assert(player != NULL);
    assert(buffer != NULL);

    // If paused or stopped, do nothing
    if(player->state == KIT_PAUSED) {
        return 0;
    }
    if(player->state == KIT_STOPPED) {
        return 0;
    }

    // If asked for nothing, don't return anything either :P
    if(length == 0) {
        return 0;
    }

    int ret = 0;
    if(SDL_LockMutex(player->amutex) == 0) {
        ret = Kit_ReadRingBuffer((Kit_RingBuffer*)player->abuffer, (char*)buffer, length);
        SDL_UnlockMutex(player->amutex);
    }
    return ret;
}

void Kit_GetPlayerInfo(const Kit_Player *player, Kit_PlayerInfo *info) {
    assert(player != NULL);
    assert(info != NULL);

    AVCodecContext *acodec_ctx = (AVCodecContext*)player->acodec_ctx;
    AVCodecContext *vcodec_ctx = (AVCodecContext*)player->vcodec_ctx;

    // Reset everything to 0. We might not fill all fields.
    memset(info, 0, sizeof(Kit_PlayerInfo));

    if(acodec_ctx != NULL) {
        strncpy(info->acodec, acodec_ctx->codec->name, KIT_CODECMAX);
        strncpy(info->acodec_name, acodec_ctx->codec->long_name, KIT_CODECNAMEMAX);
        memcpy(&info->audio, &player->aformat, sizeof(Kit_AudioFormat));
    }
    if(vcodec_ctx != NULL) {
        strncpy(info->vcodec, vcodec_ctx->codec->name, KIT_CODECMAX);
        strncpy(info->vcodec_name, vcodec_ctx->codec->long_name, KIT_CODECNAMEMAX);
        memcpy(&info->video, &player->vformat, sizeof(Kit_VideoFormat));
    }
}

KIT_API Kit_PlayerState Kit_GetPlayerState(const Kit_Player *player) {
    return player->state;
}

KIT_API void Kit_PlayerPlay(Kit_Player *player) {
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

KIT_API void Kit_PlayerStop(Kit_Player *player) {
    if(player->state == KIT_STOPPED) {
        return;
    }
    player->state = KIT_STOPPED;
}

KIT_API void Kit_PlayerPause(Kit_Player *player) {
    if(player->state != KIT_PLAYING) {
        return;
    }
    player->pause_start = _GetSystemTime();
    player->state = KIT_PAUSED;
}
