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

#define KIT_VIDEO_EARLY_FAIL 1.0
#define KIT_VIDEO_EARLY_THRESHOLD 0.005
#define KIT_VIDEO_LATE_THRESHOLD 0.05

typedef struct Kit_VideoDecoder {
    struct SwsContext *sws;       ///< Video scaler context
    AVFrame *in_frame;            ///< Raw frame from decoder
    AVFrame *out_frame;           ///< Scaled+converted frame from sws
    Kit_PacketBuffer *buffer;     ///< Packet ringbuffer for decoded video packets
    Kit_VideoOutputFormat output; ///< Output video format description
    AVFrame *current;             ///< video frame we are currently reading from
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

static void dec_flush_video_cb(Kit_Decoder *decoder) {
    assert(decoder);
    Kit_VideoDecoder *video_decoder = decoder->userdata;
    Kit_FlushPacketBuffer(video_decoder->buffer);
}

static void dec_signal_video_cb(Kit_Decoder *decoder) {
    assert(decoder);
    Kit_VideoDecoder *video_decoder = decoder->userdata;
    Kit_SignalPacketBuffer(video_decoder->buffer);
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
    // if write succeeds, no need to av_packet_unref, since Kit_WritePacketBuffer will move the refs.
    // If write fails, unref the packet. Fails should only happen if we are closing or seeking, so it is fine.
    if (!Kit_WritePacketBuffer(video_decoder->buffer, video_decoder->out_frame)) {
        av_frame_unref(video_decoder->out_frame);
    }
}

static Kit_DecoderInputResult dec_input_video_cb(const Kit_Decoder *decoder, const AVPacket *in_packet) {
    assert(decoder);
    switch(avcodec_send_packet(decoder->codec_ctx, in_packet)) {
        case AVERROR(EOF):
            return KIT_DEC_INPUT_EOF;
        case AVERROR(ENOMEM):
        case AVERROR(EAGAIN):
            return KIT_DEC_INPUT_RETRY;
        default: // Skip errors and hope for the best.
            return KIT_DEC_INPUT_OK;
    }
}

static bool dec_decode_video_cb(const Kit_Decoder *decoder, double *pts) {
    assert(decoder);
    Kit_VideoDecoder *video_decoder = decoder->userdata;
    if(avcodec_receive_frame(decoder->codec_ctx, video_decoder->in_frame) == 0) {
        *pts = video_decoder->in_frame->best_effort_timestamp * av_q2d(decoder->stream->time_base);
        dec_read_video(decoder);
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
    Kit_FreePacketBuffer(&video_decoder->buffer);
    av_frame_free(&video_decoder->in_frame);
    av_frame_free(&video_decoder->current);
    av_frame_free(&video_decoder->out_frame);
    sws_freeContext(video_decoder->sws);
    free(video_decoder);
}

Kit_Decoder* Kit_CreateVideoDecoder(const Kit_Source *src, Kit_Timer *sync_timer, int stream_index) {
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
        sync_timer,
        state->thread_count,
        dec_input_video_cb,
        dec_decode_video_cb,
        dec_flush_video_cb,
        dec_signal_video_cb,
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
        (buf_obj_move) av_frame_move_ref,
        (buf_obj_ref) av_frame_ref)) == NULL) {
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

static double Kit_GetCurrentPTS(const Kit_Decoder *decoder) {
    Kit_VideoDecoder *video_decoder = decoder->userdata;
    return video_decoder->current->best_effort_timestamp * av_q2d(decoder->stream->time_base);
}

int Kit_GetVideoDecoderData(Kit_Decoder *decoder, SDL_Texture *texture, SDL_Rect *area) {
    assert(decoder != NULL);
    assert(texture != NULL);

    Kit_VideoDecoder *video_decoder = decoder->userdata;
    SDL_Rect frame_area;
    double sync_ts;
    double pts;

    if(!Kit_BeginPacketBufferRead(video_decoder->buffer, video_decoder->current, 0))
        return 1;

    // If packet should not yet be played, stop here and wait.
    // If packet should have already been played, skip it and try to find a better packet.
    pts = Kit_GetCurrentPTS(decoder);
    sync_ts = Kit_GetTimerElapsed(decoder->sync_timer);

    // If packet is far too early, the stream jumped or was seeked. Skip packets until we see something valid.
    while(pts > sync_ts + KIT_VIDEO_EARLY_FAIL) {
        //LOG("[VIDEO] FAIL-EARLY pts = %lf > %lf + %lf\n", pts, sync_ts, KIT_VIDEO_EARLY_THRESHOLD);
        av_frame_unref(video_decoder->current);
        Kit_FinishPacketBufferRead(video_decoder->buffer);
        if(!Kit_BeginPacketBufferRead(video_decoder->buffer, video_decoder->current, 0))
            return 1;
        pts = Kit_GetCurrentPTS(decoder);
    }

    // Packet is too early, wait.
    if(pts > sync_ts + KIT_VIDEO_EARLY_THRESHOLD) {
        //LOG("[VIDEO] EARLY pts = %lf > %lf + %lf\n", pts, sync_ts, KIT_VIDEO_EARLY_THRESHOLD);
        av_frame_unref(video_decoder->current);
        Kit_CancelPacketBufferRead(video_decoder->buffer);
        return 1;
    }

    // Packet is too late, skip packets until we see something reasonable.
    while(pts < sync_ts - KIT_VIDEO_LATE_THRESHOLD) {
        //LOG("[VIDEO] LATE: pts = %lf < %lf + %lf\n", pts, sync_ts, KIT_VIDEO_LATE_THRESHOLD);
        av_frame_unref(video_decoder->current);
        Kit_FinishPacketBufferRead(video_decoder->buffer);
        if(!Kit_BeginPacketBufferRead(video_decoder->buffer, video_decoder->current, 0))
            return 1;
        pts = Kit_GetCurrentPTS(decoder);
    }
    //LOG("[VIDEO] >>> SYNC!: pts = %lf, sync = %lf\n", pts, sync_ts);

    // Update output texture with current video data.
    // Note that frame size may change on the fly. Take that into account.
    frame_area.w = video_decoder->current->width;
    frame_area.h = video_decoder->current->height;
    frame_area.x = 0;
    frame_area.y = 0;
    switch(video_decoder->output.format) {
        case SDL_PIXELFORMAT_YV12:
        case SDL_PIXELFORMAT_IYUV:
            SDL_UpdateYUVTexture(
                texture,
                &frame_area,
                video_decoder->current->data[0], video_decoder->current->linesize[0],
                video_decoder->current->data[1], video_decoder->current->linesize[1],
                video_decoder->current->data[2], video_decoder->current->linesize[2]);
            break;
        default:
            SDL_UpdateTexture(
                texture,
                &frame_area,
                video_decoder->current->data[0],
                video_decoder->current->linesize[0]);
            break;
    }
    if (area != NULL)
        *area = frame_area;

    av_frame_unref(video_decoder->current);
    decoder->aspect_ratio = video_decoder->current->sample_aspect_ratio;
    Kit_FinishPacketBufferRead(video_decoder->buffer);
    return 0;
}
