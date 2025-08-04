#include <assert.h>

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/utils/kitlog.h"
#include "kitchensink/internal/video/kitvideo.h"
#include "kitchensink/internal/video/kitvideoutils.h"
#include "kitchensink/kiterror.h"
#include "kitchensink/kitformat.h"

#define KIT_VIDEO_EARLY_FAIL 1.0
#define KIT_VIDEO_EARLY_THRESHOLD 0.005
#define KIT_VIDEO_LATE_THRESHOLD 0.05

typedef struct Kit_VideoDecoder {
    struct SwsContext *sws;       ///< Video scaler context
    AVFrame *in_frame;            ///< Raw frame from decoder
    AVFrame *out_frame;           ///< Scaled+converted frame from sws
    AVFrame *tmp_frame;           ///< Intermediary frame for HW decoding
    Kit_PacketBuffer *buffer;     ///< Packet ringbuffer for decoded video packets
    Kit_VideoOutputFormat output; ///< Output video format description
    AVFrame *current;             ///< video frame we are currently reading from
} Kit_VideoDecoder;

static struct SwsContext *Kit_GetSwsContext(
    struct SwsContext *old_context, int w, int h, enum AVPixelFormat in_fmt, enum AVPixelFormat out_fmt
) {
    struct SwsContext *new_context =
        sws_getCachedContext(old_context, w, h, in_fmt, w, h, out_fmt, SWS_BILINEAR, NULL, NULL, NULL);
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
    const Kit_VideoDecoder *video_decoder = decoder->userdata;
    memcpy(output, &video_decoder->output, sizeof(Kit_VideoOutputFormat));
    return 0;
}

static void dec_flush_video_cb(Kit_Decoder *decoder) {
    assert(decoder);
    const Kit_VideoDecoder *video_decoder = decoder->userdata;
    Kit_FlushPacketBuffer(video_decoder->buffer);
}

static void dec_signal_video_cb(Kit_Decoder *decoder) {
    assert(decoder);
    const Kit_VideoDecoder *video_decoder = decoder->userdata;
    Kit_SignalPacketBuffer(video_decoder->buffer);
}

static void dec_read_video(const Kit_Decoder *decoder) {
    Kit_VideoDecoder *video_decoder = decoder->userdata;
    const enum AVPixelFormat in_fmt = video_decoder->in_frame->format;
    const enum AVPixelFormat out_fmt = Kit_FindAVPixelFormat(video_decoder->output.format);
    const int w = video_decoder->in_frame->width;
    const int h = video_decoder->in_frame->height;

    // Convert frame format, if needed. Note that converter context MAY need to be changed here,
    // as video frame size can, in theory, change whenever.
    video_decoder->sws = Kit_GetSwsContext(video_decoder->sws, w, h, in_fmt, out_fmt);
    sws_scale_frame(video_decoder->sws, video_decoder->out_frame, video_decoder->in_frame);
    av_frame_copy_props(video_decoder->out_frame, video_decoder->in_frame);

    // Write video packet to packet buffer. This may block!
    // - if write succeeds, no need to av_packet_unref, since Kit_WritePacketBuffer will move the refs.
    // - If write fails, unref the packet. Fails should only happen if we are closing or seeking, so it is fine.
    if(!Kit_WritePacketBuffer(video_decoder->buffer, video_decoder->out_frame)) {
        av_frame_unref(video_decoder->out_frame);
    }
}

static Kit_DecoderInputResult dec_input_video_cb(const Kit_Decoder *decoder, const AVPacket *in_packet) {
    assert(decoder);
    switch(avcodec_send_packet(decoder->codec_ctx, in_packet)) {
        case AVERROR_EOF:
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
    if(avcodec_receive_frame(decoder->codec_ctx, video_decoder->tmp_frame) == 0) {
        // Process the temporary frame, and then make sure result is in in_frame.
        // If the frame is hardware frame, we need to pull it from the hardware device first!
        if(video_decoder->tmp_frame->format == decoder->hw_fmt) {
            if(av_hwframe_transfer_data(video_decoder->in_frame, video_decoder->tmp_frame, 0) < 0) {
                return false;
            }
            av_frame_copy_props(video_decoder->in_frame, video_decoder->tmp_frame);
            av_frame_unref(video_decoder->tmp_frame);
        } else {
            av_frame_move_ref(video_decoder->in_frame, video_decoder->tmp_frame);
        }

        // Process input frame (if HW decoding is used, it has been pulled from the GPU).
        *pts = video_decoder->in_frame->best_effort_timestamp * av_q2d(decoder->stream->time_base);
        dec_read_video(decoder);
        av_frame_unref(video_decoder->in_frame);
        return true;
    }
    return false;
}

static void dec_get_video_buffers_cb(const Kit_Decoder *ref, unsigned int *length, unsigned int *capacity) {
    assert(ref);
    assert(ref->userdata);
    Kit_VideoDecoder *video_decoder = ref->userdata;
    if(length != NULL)
        *length = Kit_GetPacketBufferLength(video_decoder->buffer);
    if(capacity != NULL)
        *capacity = Kit_GetPacketBufferCapacity(video_decoder->buffer);
}

static void dec_close_video_cb(Kit_Decoder *ref) {
    if(ref == NULL)
        return;
    assert(ref->userdata);
    Kit_VideoDecoder *video_decoder = ref->userdata;
    Kit_FreePacketBuffer(&video_decoder->buffer);
    av_frame_free(&video_decoder->in_frame);
    av_frame_free(&video_decoder->tmp_frame);
    av_frame_free(&video_decoder->current);
    av_frame_free(&video_decoder->out_frame);
    sws_freeContext(video_decoder->sws);
    free(video_decoder);
}

Kit_Decoder *Kit_CreateVideoDecoder(
    const Kit_Source *src,
    const Kit_VideoFormatRequest *format_request,
    Kit_Timer *sync_timer,
    const int stream_index
) {
    assert(src != NULL);

    const Kit_LibraryState *state = Kit_GetLibraryState();
    const AVFormatContext *format_ctx = src->format_ctx;
    AVStream *stream = NULL;
    Kit_VideoDecoder *video_decoder = NULL;
    Kit_Decoder *decoder = NULL;
    Kit_PacketBuffer *buffer = NULL;
    AVFrame *in_frame = NULL;
    AVFrame *out_frame = NULL;
    AVFrame *tmp_frame = NULL;
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
            format_request->hw_device_types,
            dec_input_video_cb,
            dec_decode_video_cb,
            dec_flush_video_cb,
            dec_signal_video_cb,
            dec_close_video_cb,
            dec_get_video_buffers_cb,
            video_decoder
        )) == NULL) {
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
    if((tmp_frame = av_frame_alloc()) == NULL) {
        Kit_SetError("Unable to allocate temporary hardware video frame for stream %d", stream_index);
        goto exit_4;
    }
    if((current = av_frame_alloc()) == NULL) {
        Kit_SetError("Unable to allocate temporary flip video frame for stream %d", stream_index);
        goto exit_5;
    }
    if((buffer = Kit_CreatePacketBuffer(
            state->video_frame_buffer_size,
            (buf_obj_alloc)av_frame_alloc,
            (buf_obj_unref)av_frame_unref,
            (buf_obj_free)av_frame_free,
            (buf_obj_move)av_frame_move_ref,
            (buf_obj_ref)av_frame_ref
        )) == NULL) {
        Kit_SetError("Unable to create an output buffer for stream %d", stream_index);
        goto exit_6;
    }

    // Set format configs
    memset(&output, 0, sizeof(Kit_VideoOutputFormat));
    if(format_request->format != SDL_PIXELFORMAT_UNKNOWN) {
        output_format = Kit_FindAVPixelFormat(format_request->format);
        output.format = format_request->format;
    } else {
        output_format = Kit_FindBestAVPixelFormat(decoder->codec_ctx->pix_fmt);
        output.format = Kit_FindSDLPixelFormat(output_format);
    }
    if(output_format == AV_PIX_FMT_NONE) {
        Kit_SetError("Unsupported output pixel format");
        goto exit_7;
    }
    output.width = (format_request->width > -1) ? format_request->width : decoder->codec_ctx->width;
    output.height = (format_request->height > -1) ? format_request->height : decoder->codec_ctx->height;
    output.hw_device_type = Kit_FindHWDeviceType(decoder->hw_type);

    // Create scaler for handling format changes
    sws = Kit_GetSwsContext(sws, output.width, output.height, decoder->codec_ctx->pix_fmt, output_format);
    if(sws == NULL) {
        goto exit_7;
    }

    video_decoder->in_frame = in_frame;
    video_decoder->out_frame = out_frame;
    video_decoder->tmp_frame = tmp_frame;
    video_decoder->current = current;
    video_decoder->sws = sws;
    video_decoder->buffer = buffer;
    video_decoder->output = output;
    return decoder;

exit_7:
    Kit_FreePacketBuffer(&buffer);
exit_6:
    av_frame_free(&current);
exit_5:
    av_frame_free(&tmp_frame);
exit_4:
    av_frame_free(&out_frame);
exit_3:
    av_frame_free(&in_frame);
exit_2:
    Kit_CloseDecoder(&decoder);
    return NULL; // Above frees the video_decoder also.
exit_1:
    free(video_decoder);
exit_0:
    return NULL;
}

static double Kit_GetCurrentPTS(const Kit_Decoder *decoder) {
    Kit_VideoDecoder *video_decoder = decoder->userdata;
    return video_decoder->current->best_effort_timestamp * av_q2d(decoder->stream->time_base);
}

bool Kit_BeginReadFrame(const Kit_Decoder *decoder) {
    assert(decoder != NULL);
    const Kit_VideoDecoder *video_decoder = decoder->userdata;

    if(!Kit_BeginPacketBufferRead(video_decoder->buffer, video_decoder->current, 0))
        return false;

    // Initialize timer if it's the primary sync source, and it's not yet initialized.
    Kit_InitTimerBase(decoder->sync_timer);
    if(!Kit_IsTimerInitialized(decoder->sync_timer)) {
        // If this was not the sync source and timer is not set, wait for another stream to set it.
        av_frame_unref(video_decoder->current);
        Kit_CancelPacketBufferRead(video_decoder->buffer);
        return false;
    }

    double pts = Kit_GetCurrentPTS(decoder);
    double sync_ts = Kit_GetTimerElapsed(decoder->sync_timer);

    // If packet is far too early, the stream jumped or was seeked.
    if (Kit_IsTimerPrimary(decoder->sync_timer)) {
        // If this stream is the sync source, then reset this as the new sync timestamp.
        if(pts > sync_ts + KIT_VIDEO_EARLY_FAIL) {
            // LOG("[VIDEO] NO SYNC pts = %lf > %lf + %lf\n", pts, sync_ts, KIT_VIDEO_EARLY_FAIL);
            Kit_AddTimerBase(decoder->sync_timer, -(pts - sync_ts));
            sync_ts = Kit_GetTimerElapsed(decoder->sync_timer);
        }
    } else {
        while(pts > sync_ts + KIT_VIDEO_EARLY_FAIL) {
            // LOG("[VIDEO] FAIL-EARLY pts = %lf > %lf + %lf\n", pts, sync_ts, KIT_VIDEO_EARLY_FAIL);
            av_frame_unref(video_decoder->current);
            Kit_FinishPacketBufferRead(video_decoder->buffer);
            if(!Kit_BeginPacketBufferRead(video_decoder->buffer, video_decoder->current, 0))
                return false;
            pts = Kit_GetCurrentPTS(decoder);
        }
    }

    // Packet is too early, wait.
    if(pts > sync_ts + KIT_VIDEO_EARLY_THRESHOLD) {
        //LOG("[VIDEO] EARLY pts = %lf > %lf + %lf\n", pts, sync_ts, KIT_VIDEO_EARLY_THRESHOLD);
        av_frame_unref(video_decoder->current);
        Kit_CancelPacketBufferRead(video_decoder->buffer);
        return false;
    }

    // Packet is too late, skip packets until we see something reasonable.
    while(pts < sync_ts - KIT_VIDEO_LATE_THRESHOLD) {
        //LOG("[VIDEO] LATE: pts = %lf < %lf + %lf\n", pts, sync_ts, KIT_VIDEO_LATE_THRESHOLD);
        av_frame_unref(video_decoder->current);
        Kit_FinishPacketBufferRead(video_decoder->buffer);
        if(!Kit_BeginPacketBufferRead(video_decoder->buffer, video_decoder->current, 0))
            return false;
        pts = Kit_GetCurrentPTS(decoder);
    }

    // LOG("[VIDEO] >>> SYNC!: pts = %lf, sync = %lf\n", pts, sync_ts);
    return true;
}

void Kit_EndReadFrame(Kit_Decoder *decoder) {
    const Kit_VideoDecoder *video_decoder = decoder->userdata;
    av_frame_unref(video_decoder->current);
    Kit_FinishPacketBufferRead(video_decoder->buffer);
}

int Kit_GetVideoDecoderSDLTexture(Kit_Decoder *decoder, SDL_Texture *texture, SDL_Rect *area) {
    assert(decoder != NULL);
    assert(texture != NULL);
    const Kit_VideoDecoder *video_decoder = decoder->userdata;

    // Try to read and sync frame. If this fails, then there is nothing else to do other than wait.
    if(!Kit_BeginReadFrame(decoder)) {
        return 1;
    }

    // Update output texture with current video data.
    // Note that frame size may change on the fly. Take that into account.
    SDL_Rect frame_area;
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
                video_decoder->current->data[0],
                video_decoder->current->linesize[0],
                video_decoder->current->data[1],
                video_decoder->current->linesize[1],
                video_decoder->current->data[2],
                video_decoder->current->linesize[2]
            );
            break;
        default:
            SDL_UpdateTexture(
                texture, &frame_area, video_decoder->current->data[0], video_decoder->current->linesize[0]
            );
            break;
    }
    if(area != NULL)
        *area = frame_area;

    decoder->aspect_ratio = video_decoder->current->sample_aspect_ratio;

    Kit_EndReadFrame(decoder);
    return 0;
}

int Kit_LockVideoDecoderRaw(Kit_Decoder *decoder, unsigned char ***data, int **line_size, SDL_Rect *area) {
    assert(decoder != NULL);
    const Kit_VideoDecoder *video_decoder = decoder->userdata;

    // Try to read and sync frame. If this fails, then there is nothing else to do other than wait.
    if(!Kit_BeginReadFrame(decoder)) {
        return 1;
    }

    // Copy pointers.
    if(line_size != NULL) {
        *line_size = video_decoder->current->linesize;
    }
    if(data != NULL) {
        *data = video_decoder->current->data;
    }
    if(area != NULL) {
        area->w = video_decoder->current->width;
        area->h = video_decoder->current->height;
        area->x = 0;
        area->y = 0;
    }
    decoder->aspect_ratio = video_decoder->current->sample_aspect_ratio;
    return 0;
}

void Kit_UnlockVideoDecoderRaw(Kit_Decoder *decoder) {
    assert(decoder != NULL);
    Kit_EndReadFrame(decoder);
}