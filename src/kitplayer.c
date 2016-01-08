#include "kitchensink/kitplayer.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitbuffer.h"
#include "kitchensink/internal/kitringbuffer.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/pixfmt.h>

#include <SDL2/SDL_types.h>
#include <SDL2/SDL_pixels.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

    // Make sure indexes seem correct
    AVFormatContext *format_ctx = (AVFormatContext *)src->format_ctx;
    if(src->astream_idx < 0 || src->astream_idx >= format_ctx->nb_streams) {
        Kit_SetError("Invalid audio stream index");
        return 1;
    }
    if(src->vstream_idx < 0 || src->vstream_idx >= format_ctx->nb_streams) {
        Kit_SetError("Invalid video stream index");
        return 1;
    }

    // Find video decoder
    vcodec = avcodec_find_decoder(format_ctx->streams[src->vstream_idx]->codec->codec_id);
    if(!vcodec) {
        Kit_SetError("No suitable video decoder found");
        goto exit_0;
    }

    // Copy the original video codec context
    vcodec_ctx = avcodec_alloc_context3(vcodec);
    if(avcodec_copy_context(vcodec_ctx, format_ctx->streams[src->vstream_idx]->codec) != 0) {
        Kit_SetError("Unable to copy video codec context");
        goto exit_0;
    }

    // Create a video decoder context
    if(avcodec_open2(vcodec_ctx, vcodec, NULL) < 0) {
        Kit_SetError("Unable to allocate video codec context");
        goto exit_1;
    }

    // Find audio decoder
    acodec = avcodec_find_decoder(format_ctx->streams[src->astream_idx]->codec->codec_id);
    if(!acodec) {
        Kit_SetError("No suitable audio decoder found");
        goto exit_2;
    }

    // Copy the original audio codec context
    acodec_ctx = avcodec_alloc_context3(acodec);
    if(avcodec_copy_context(acodec_ctx, format_ctx->streams[src->astream_idx]->codec) != 0) {
        Kit_SetError("Unable to copy audio codec context");
        goto exit_2;
    }

    // Create an audio decoder context
    if(avcodec_open2(acodec_ctx, acodec, NULL) < 0) {
        Kit_SetError("Unable to allocate audio codec context");
        goto exit_3;
    }

    player->acodec_ctx = acodec_ctx;
    player->vcodec_ctx = vcodec_ctx;
    player->src = src;
    return 0;

exit_3:
    avcodec_free_context(&acodec_ctx);
exit_2:
    avcodec_close(vcodec_ctx);
exit_1:
    avcodec_free_context(&vcodec_ctx);
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
    av_free(&packet->frame);
    free(packet);
}

static Kit_VideoPacket* _CreateVideoPacket(AVPicture *frame, double pts) {
    Kit_VideoPacket *p = calloc(1, sizeof(Kit_VideoPacket));
    p->frame = frame;
    p->pts = pts;
    return p;
}

Kit_Player* Kit_CreatePlayer(const Kit_Source *src) {
    assert(src != NULL);

    Kit_Player *player = calloc(1, sizeof(Kit_Player));

    // Initialize codecs
    if(_InitCodecs(player, src) != 0) {
        goto exit_0;
    }

    AVCodecContext *acodec_ctx = (AVCodecContext*)player->acodec_ctx;
    AVCodecContext *vcodec_ctx = (AVCodecContext*)player->vcodec_ctx;
    player->vformat.width = vcodec_ctx->width;
    player->vformat.height = vcodec_ctx->height;
    _FindPixelFormat(vcodec_ctx->pix_fmt, &player->vformat.format);
    player->aformat.samplerate = acodec_ctx->sample_rate;
    player->aformat.channels = acodec_ctx->channels;
    _FindAudioFormat(acodec_ctx->sample_fmt, &player->aformat.bytes, &player->aformat.is_signed);

    // Audio converter
    struct SwrContext *swr = swr_alloc_set_opts(
        NULL,
        _FindAVChannelLayout(player->aformat.channels), // Target channel layout
        _FindAVSampleFormat(player->aformat.bytes), // Target fmt
        player->aformat.samplerate, // Target samplerate
        acodec_ctx->channel_layout, // Source channel layout
        acodec_ctx->sample_fmt, // Source fmt
        acodec_ctx->sample_rate, // Source samplerate
        0, NULL);
    swr_init(swr);

    // Video converter
    struct SwsContext *sws = sws_getContext(
        vcodec_ctx->width, // Source w
        vcodec_ctx->height, // Source h
        vcodec_ctx->pix_fmt, // Source fmt
        vcodec_ctx->width, // Target w
        vcodec_ctx->height, // Target h
        _FindAVPixelFormat(player->vformat.format), // Target fmt
        SWS_BICUBIC,
        NULL, NULL, NULL);

    player->swr = swr;
    player->sws = sws;
    player->abuffer = Kit_CreateRingBuffer(KIT_ABUFFERSIZE);
    player->vbuffer = Kit_CreateBuffer(KIT_VBUFFERSIZE);
    player->tmp_vframe = av_frame_alloc();
    player->tmp_aframe = av_frame_alloc();
    return player;

exit_0:
    free(player);
    return NULL;
}

void Kit_ClosePlayer(Kit_Player *player) {
    if(player == NULL) return;
    sws_freeContext((struct SwsContext *)player->sws);
    swr_free((struct SwrContext **)&player->swr);
    av_frame_free((AVFrame**)&player->tmp_vframe);
    av_frame_free((AVFrame**)&player->tmp_aframe);
    avcodec_close((AVCodecContext*)player->acodec_ctx);
    avcodec_close((AVCodecContext*)player->vcodec_ctx);
    avcodec_free_context((AVCodecContext**)&player->acodec_ctx);
    avcodec_free_context((AVCodecContext**)&player->vcodec_ctx);
    Kit_DestroyRingBuffer((Kit_RingBuffer*)player->abuffer);
    Kit_VideoPacket *p;
    while((p = Kit_ReadBuffer(player->vbuffer)) != NULL) {
        _FreeVideoPacket(p);
    }
    Kit_DestroyBuffer((Kit_Buffer*)player->vbuffer);
    free(player);
}

void _HandleVideoPacket(Kit_Player *player, AVPacket *packet) {
    assert(player != NULL);
    assert(packet != NULL);
    
    int frame_finished;
    AVCodecContext *vcodec_ctx = (AVCodecContext*)player->vcodec_ctx;
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

        // Save to buffer
        Kit_WriteBuffer(
            (Kit_Buffer*)player->vbuffer,
            _CreateVideoPacket(oframe, 0));
    }
}

void _HandleAudioPacket(Kit_Player *player, AVPacket *packet) {
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

            Kit_WriteRingBuffer((Kit_RingBuffer*)player->abuffer, (char*)dst_data[0], dst_bufsize);
            av_freep(&dst_data[0]);
            av_freep(&dst_data);
        }
    }
}

// Return 0 if stream is good but nothing else to do for now
// Return -1 if there is still work to be done
// Return 1 if there was an error or stream end
int Kit_UpdatePlayer(Kit_Player *player) {
    assert(player != NULL);

    // If either buffer is full, just stop here for now.
    if(Kit_IsBufferFull(player->vbuffer)) {
        return 0;
    }

    AVPacket packet;
    AVFormatContext *format_ctx = (AVFormatContext*)player->src->format_ctx;

    // Attempt to read frame. Just return here if it fails.
    if(av_read_frame(format_ctx, &packet) < 0) {
        return 1;
    }

    // Check if this is a packet we need to handle and pass it on
    if(packet.stream_index == player->src->vstream_idx) {
        _HandleVideoPacket(player, &packet);
    }
    if(packet.stream_index == player->src->astream_idx) {
        _HandleAudioPacket(player, &packet);
    }

    // Free packet and that's that
    av_free_packet(&packet);
    return -1;
}

int Kit_RefreshTexture(Kit_Player *player, SDL_Texture *texture) {
    assert(player != NULL);
    assert(texture != NULL);

    int retval = 1;

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

    Kit_VideoPacket *packet = (Kit_VideoPacket*)Kit_ReadBuffer((Kit_Buffer*)player->vbuffer);
    if(packet == NULL) {
        return 0;
    }

    if(format == SDL_PIXELFORMAT_YV12 || format == SDL_PIXELFORMAT_IYUV) {
        SDL_UpdateYUVTexture(
            texture, NULL, 
            packet->frame->data[0], packet->frame->linesize[0],
            packet->frame->data[1], packet->frame->linesize[1],
            packet->frame->data[2], packet->frame->linesize[2]);
    }
    else if(access == SDL_TEXTUREACCESS_STREAMING) {
        unsigned char *pixels;
        int pitch;

        if(SDL_LockTexture(texture, NULL, (void**)&pixels, &pitch) == 0) {
            memcpy(pixels, packet->frame->data[0], pitch * h);
            SDL_UnlockTexture(texture);
        } else {
            Kit_SetError("Unable to lock texture for streaming access");
            goto exit_0;
        }
    }
    else {
        Kit_SetError("Incorrect texture access");
        goto exit_0;
    }

    retval = 0;
exit_0:
    _FreeVideoPacket(packet);
    return retval;
}

int Kit_GetAudioData(Kit_Player *player, unsigned char *buffer, size_t length) {
    assert(player != NULL);
    assert(buffer != NULL);

    if(length == 0) {
        return 0;
    }

    int ret = Kit_ReadRingBuffer((Kit_RingBuffer*)player->abuffer, (char*)buffer, length);
    return ret;
}

void Kit_GetPlayerInfo(const Kit_Player *player, Kit_PlayerInfo *info) {
    assert(player != NULL);
    assert(info != NULL);

    AVCodecContext *acodec_ctx = (AVCodecContext*)player->acodec_ctx;
    AVCodecContext *vcodec_ctx = (AVCodecContext*)player->vcodec_ctx;

    strncpy(info->acodec, acodec_ctx->codec->name, KIT_CODECMAX);
    strncpy(info->acodec_name, acodec_ctx->codec->long_name, KIT_CODECNAMEMAX);
    strncpy(info->vcodec, vcodec_ctx->codec->name, KIT_CODECMAX);
    strncpy(info->vcodec_name, vcodec_ctx->codec->long_name, KIT_CODECNAMEMAX);

    memcpy(&info->video, &player->vformat, sizeof(Kit_VideoFormat));
    memcpy(&info->audio, &player->aformat, sizeof(Kit_AudioFormat));
}
