#include "kitchensink/kitplayer.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitbuffer.h"
#include "kitchensink/internal/kitringbuffer.h"
#include "kitchensink/internal/kitlist.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/kitvideo.h"
#include "kitchensink/internal/kitaudio.h"
#include "kitchensink/internal/kitsubtitle.h"
#include "kitchensink/internal/kitcontrol.h"
#include "kitchensink/internal/kithelpers.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>

#include <SDL2/SDL.h>
#include <ass/ass.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

// For compatibility
#ifndef ASS_FONTPROVIDER_AUTODETECT
#define ASS_FONTPROVIDER_AUTODETECT 1
#endif

// Threshold is in seconds
#define VIDEO_SYNC_THRESHOLD 0.01
#define AUDIO_SYNC_THRESHOLD 0.05

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
    av_packet_unref(&packet);
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
        player->aformat.channels = acodec_ctx->channels > 2 ? 2 : acodec_ctx->channels;
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
        ass_set_fonts(player->ass_renderer, NULL, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, NULL, 1);
        ass_set_frame_size(player->ass_renderer, vcodec_ctx->width, vcodec_ctx->height);
        ass_set_hinting(player->ass_renderer, ASS_HINTING_NONE);

        // Initialize libass track
        player->ass_track = ass_new_track(state->libass_handle);
        if(player->ass_track == NULL) {
            Kit_SetError("Unable to initialize libass track");
            goto error;
        }

        // Set up libass track headers (ffmpeg provides these)
        if(scodec_ctx->subtitle_header) {
            ass_process_codec_private(
                (ASS_Track*)player->ass_track,
                (char*)scodec_ctx->subtitle_header,
                scodec_ctx->subtitle_header_size);
        }
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
        return 0;
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
            ret = Kit_ReadRingBuffer(packet->rb, (char*)buffer, length);
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
        return 0;
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
