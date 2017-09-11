#include <assert.h>

#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>

#include "kitchensink/internal/kithelpers.h"
#include "kitchensink/internal/kitbuffer.h"
#include "kitchensink/internal/kitaudio.h"


Kit_AudioPacket* _CreateAudioPacket(const char* data, size_t len, double pts) {
    Kit_AudioPacket *p = calloc(1, sizeof(Kit_AudioPacket));
    p->rb = Kit_CreateRingBuffer(len);
    Kit_WriteRingBuffer(p->rb, data, len);
    p->pts = pts;
    return p;
}

void _FreeAudioPacket(void *ptr) {
    Kit_AudioPacket *packet = ptr;
    Kit_DestroyRingBuffer(packet->rb);
    free(packet);
}

enum AVSampleFormat _FindAVSampleFormat(int format) {
    switch(format) {
        case AUDIO_U8: return AV_SAMPLE_FMT_U8;
        case AUDIO_S16SYS: return AV_SAMPLE_FMT_S16;
        case AUDIO_S32SYS: return AV_SAMPLE_FMT_S32;
        default:
            return AV_SAMPLE_FMT_NONE;
    }
}

unsigned int _FindAVChannelLayout(int channels) {
    switch(channels) {
        case 1: return AV_CH_LAYOUT_MONO;
        case 2: return AV_CH_LAYOUT_STEREO;
        case 4: return AV_CH_LAYOUT_QUAD;
        case 6: return AV_CH_LAYOUT_5POINT1;
        default: return AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
}

void _FindAudioFormat(enum AVSampleFormat fmt, int *bytes, bool *is_signed, unsigned int *format) {
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

void _HandleAudioPacket(Kit_Player *player, AVPacket *packet) {
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

    while(packet->size > 0) {
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

        packet->size -= len;
        packet->data += len;
    }
}
