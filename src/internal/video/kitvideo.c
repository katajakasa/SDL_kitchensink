#include <assert.h>

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/kitformat.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/utils/kitlog.h"
#include "kitchensink/internal/video/kitvideoutils.h"
#include "kitchensink/internal/video/kitvideo.h"

#define KIT_VIDEO_SYNC_THRESHOLD 0.02


typedef struct Kit_VideoDecoder {
    struct SwsContext *sws;       ///< Video scaler context
    AVFrame *in_frame;            ///< Raw frame from decoder
    AVFrame *out_frame;           ///< Scaled+converted frame from sws
    Kit_PacketBuffer *buffer;     ///< Packet ringbuffer for decoded video packets
    Kit_VideoOutputFormat output; ///< Output video format description
    AVFrame *current;             ///< video frame we are currently reading from
    bool valid_current;           ///< Whether the current frame has valid data
} Kit_VideoDecoder;


static struct SwsContext* Kit_GetSwsContext(
    struct SwsContext *old_context,
    int w,
    int h,
    enum AVPixelFormat in_fmt,
    enum AVPixelFormat out_fmt
) {
    struct SwsContext* new_context = sws_getCachedContext(
        old_context,
        w,
        h,
        in_fmt,
        w,
        h,
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

int Kit_GetVideoDecoderOutputFormat(const Kit_Decoder *decoder, Kit_VideoOutputFormat *output) {
    if(decoder == NULL) {
        memset(output, 0, sizeof(Kit_VideoOutputFormat));
        return 1;
    }
    Kit_VideoDecoder *video_decoder = decoder->userdata;
    memcpy(output, &video_decoder->output, sizeof(Kit_VideoOutputFormat));
    return 0;
}

static void dec_read_video(const Kit_Decoder *decoder) {
    Kit_VideoDecoder *video_decoder = decoder->userdata;
    enum AVPixelFormat in_fmt = decoder->codec_ctx->pix_fmt;
    enum AVPixelFormat out_fmt = Kit_FindAVPixelFormat(video_decoder->output.format);
    int w = video_decoder->in_frame->width;
    int h = video_decoder->in_frame->height;

    video_decoder->sws = Kit_GetSwsContext(
        video_decoder->sws, w, h, in_fmt, out_fmt);
    sws_scale_frame(video_decoder->sws, video_decoder->out_frame, video_decoder->in_frame);
    av_frame_copy_props(video_decoder->out_frame, video_decoder->in_frame);

    // Write video packet to packet buffer. This may block!
    // No need to av_packet_unref, since Kit_WritePacketBuffer will move the refs.
    Kit_WritePacketBuffer(video_decoder->buffer, video_decoder->out_frame);
}

static bool dec_input_video_cb(const Kit_Decoder *dec, const AVPacket *in_packet) {
    assert(dec != NULL);
    assert(in_packet != NULL);
    return avcodec_send_packet(dec->codec_ctx, in_packet) < 0;
}

static bool dec_decode_video_cb(const Kit_Decoder *dec) {
    assert(dec != NULL);
    Kit_VideoDecoder *video_decoder = dec->userdata;
    if(avcodec_receive_frame(dec->codec_ctx, video_decoder->in_frame) == 0) {
        dec_read_video(dec);
        av_frame_unref(video_decoder->in_frame);
        return true;
    }
    return false;
}

static void dec_close_video_cb(Kit_Decoder *ref) {
    if(ref == NULL)
        return;
    assert(ref->userdata);
    Kit_VideoDecoder *video_decoder = ref->userdata;
    if(video_decoder->buffer != NULL)
        Kit_FreePacketBuffer(&video_decoder->buffer);
    if(video_decoder->in_frame != NULL)
        av_frame_free(&video_decoder->in_frame);
    if(video_decoder->current != NULL)
        av_frame_free(&video_decoder->current);
    if(video_decoder->out_frame != NULL)
        av_frame_free(&video_decoder->out_frame);
    if(video_decoder->sws != NULL)
        sws_freeContext(video_decoder->sws);
    free(video_decoder);
}

Kit_Decoder* Kit_CreateVideoDecoder(const Kit_Source *src, int stream_index) {
    assert(src != NULL);

    const Kit_LibraryState *state = Kit_GetLibraryState();
    const AVFormatContext *format_ctx = src->format_ctx;
    AVStream *stream = NULL;
    Kit_VideoDecoder *video_decoder = NULL;
    Kit_Decoder *decoder = NULL;
    Kit_PacketBuffer *buffer = NULL;
    AVFrame *in_frame = NULL;
    AVFrame *out_frame = NULL;
    AVFrame *current = NULL;
    struct SwsContext *sws = NULL;
    Kit_VideoOutputFormat output;
    enum AVPixelFormat output_format;

    // Find and set up stream.
    if(stream_index < 0 || stream_index >= format_ctx->nb_streams) {
        Kit_SetError("Invalid video stream index %d", stream_index);
        return NULL;
    }
    stream = format_ctx->streams[stream_index];

    if((video_decoder = calloc(1, sizeof(Kit_VideoDecoder))) == NULL) {
        Kit_SetError("Unable to allocate video decoder for stream %d", stream_index);
        goto exit_0;
    }
    if((decoder = Kit_CreateDecoder(
        stream,
        state->thread_count,
        dec_input_video_cb,
        dec_decode_video_cb,
        dec_close_video_cb,
        video_decoder)) == NULL) {
        // No need to Kit_SetError, it will be set in Kit_CreateDecoder.
        goto exit_1;
    }
    if((in_frame = av_frame_alloc()) == NULL) {
        Kit_SetError("Unable to allocate temporary input video frame for stream %d", stream_index);
        goto exit_2;
    }
    if((out_frame = av_frame_alloc()) == NULL) {
        Kit_SetError("Unable to allocate temporary output video frame for stream %d", stream_index);
        goto exit_3;
    }
    if((current = av_frame_alloc()) == NULL) {
        Kit_SetError("Unable to allocate temporary flip video frame for stream %d", stream_index);
        goto exit_4;
    }
    if((buffer = Kit_CreatePacketBuffer(
        2,
        (buf_obj_alloc) av_frame_alloc,
        (buf_obj_unref) av_frame_unref,
        (buf_obj_free) av_frame_free,
        (buf_obj_move) av_frame_move_ref)) == NULL) {
        Kit_SetError("Unable to create an output buffer for stream %d", stream_index);
        goto exit_5;
    }

    // Set format configs
    output_format = Kit_FindBestAVPixelFormat(decoder->codec_ctx->pix_fmt);
    memset(&output, 0, sizeof(Kit_VideoOutputFormat));
    output.width = decoder->codec_ctx->width;
    output.height = decoder->codec_ctx->height;
    output.format = Kit_FindSDLPixelFormat(output_format);

    // Create scaler for handling format changes
    if((sws = Kit_GetSwsContext(
        sws,
        decoder->codec_ctx->width,
        decoder->codec_ctx->height,
        decoder->codec_ctx->pix_fmt,
        output_format)) == NULL) {
        goto exit_6;
    }

    video_decoder->in_frame = in_frame;
    video_decoder->out_frame = out_frame;
    video_decoder->current = current;
    video_decoder->sws = sws;
    video_decoder->buffer = buffer;
    video_decoder->output = output;
    video_decoder->valid_current = false;
    return decoder;

exit_6:
    Kit_FreePacketBuffer(&buffer);
exit_5:
    av_frame_free(&current);
exit_4:
    av_frame_free(&out_frame);
exit_3:
    av_frame_free(&in_frame);
exit_2:
    Kit_CloseDecoder(&decoder);
exit_1:
    free(video_decoder);
exit_0:
    return NULL;
}

static bool Kit_GetNextVideoFrame(Kit_VideoDecoder *video_decoder) {
    if(video_decoder->valid_current)
        return true;
    av_frame_unref(video_decoder->current);
    video_decoder->valid_current = Kit_ReadPacketBuffer(video_decoder->buffer, video_decoder->current, 0);
    return video_decoder->valid_current;
}

static double Kit_GetCurrentPTS(const Kit_Decoder *decoder) {
    Kit_VideoDecoder *video_decoder = decoder->userdata;
    return video_decoder->current->best_effort_timestamp * av_q2d(decoder->stream->time_base);
}

int Kit_GetVideoDecoderData(Kit_Decoder *decoder, SDL_Texture *texture, SDL_Rect *area) {
    assert(decoder != NULL);
    assert(texture != NULL);

    Kit_VideoDecoder *video_decoder = decoder->userdata;
    double sync_ts;
    double pts;

    // If we have no frame selected, try to get another one from the decoder output buffer.
    if(!Kit_GetNextVideoFrame(video_decoder))
        return 1;

    // If packet should not yet be played, stop here and wait.
    // If packet should have already been played, skip it and try to find a better packet.
    pts = Kit_GetCurrentPTS(decoder);
    sync_ts = Kit_GetSystemTime() - decoder->clock_sync;
    if(pts > sync_ts + KIT_VIDEO_SYNC_THRESHOLD) {
        return 1;
    }
    while(pts < sync_ts - KIT_VIDEO_SYNC_THRESHOLD) {
        video_decoder->valid_current = false;
        if(!Kit_GetNextVideoFrame(video_decoder))
            return 1;
        pts = Kit_GetCurrentPTS(decoder);
    }

    // Update output texture with current video data.
    // Note that frame size may change on the fly. Take that into account.
    area->w = video_decoder->current->width;
    area->h = video_decoder->current->height;
    area->x = 0;
    area->y = 0;
    switch(video_decoder->output.format) {
        case SDL_PIXELFORMAT_YV12:
        case SDL_PIXELFORMAT_IYUV:
            SDL_UpdateYUVTexture(
                texture, area,
                video_decoder->current->data[0], video_decoder->current->linesize[0],
                video_decoder->current->data[1], video_decoder->current->linesize[1],
                video_decoder->current->data[2], video_decoder->current->linesize[2]);
            break;
        default:
            SDL_UpdateTexture(
                texture, area,
                video_decoder->current->data[0],
                video_decoder->current->linesize[0]);
            break;
    }

    decoder->clock_pos = pts;
    decoder->aspect_ratio = video_decoder->current->sample_aspect_ratio;
    video_decoder->valid_current = false;
    return 0;
}
