#include <assert.h>
#include <inttypes.h>

#include <SDL.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>

#include "kitchensink/internal/audio/kitaudio.h"
#include "kitchensink/internal/audio/kitaudioutils.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/utils/kitlog.h"
#include "kitchensink/kiterror.h"

#define KIT_AUDIO_EARLY_FAIL 5.0

#define SAMPLE_BYTES(audio_decoder) (audio_decoder->output.channels * audio_decoder->output.bytes)

typedef struct Kit_AudioDecoder {
    SwrContext *swr;              ///< Audio resampler context
    AVFrame *in_frame;            ///< Temporary AVFrame for audio decoding purposes
    AVFrame *out_frame;           ///< Temporary AVFrame fur audio resampling purposes
    AVFrame *current;             ///< Audio packet we are currently reading from
    AVAudioFifo *fifo;            ///< FIFO for splitting and/or joining audio packets
    int64_t fifo_start_pts;       ///< Audio fifo start PTS
    Kit_PacketBuffer *buffer;     ///< Packet ringbuffer for decoded audio packets
    Kit_AudioOutputFormat output; ///< Output audio format description
} Kit_AudioDecoder;

int Kit_GetAudioDecoderOutputFormat(const Kit_Decoder *decoder, Kit_AudioOutputFormat *output) {
    if(decoder == NULL) {
        memset(output, 0, sizeof(Kit_AudioOutputFormat));
        return 1;
    }
    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    memcpy(output, &audio_decoder->output, sizeof(Kit_AudioOutputFormat));
    return 0;
}

static void write_packet(Kit_AudioDecoder *audio_decoder, int nb_samples) {
    int total_bytes = SAMPLE_BYTES(audio_decoder) * nb_samples;

    // Save bytes left in the frame. Misuse crop values, since they are only used for video packets by ffmpeg.
    audio_decoder->out_frame->crop_top = total_bytes;
    audio_decoder->out_frame->crop_bottom = total_bytes;

    // Write video packet to packet buffer. This may block!
    // if write succeeds, no need to av_packet_unref, since Kit_WritePacketBuffer will move the refs.
    // If write fails, unref the packet. Fails should only happen if we are closing or seeking, so it is fine.
    if(!Kit_WritePacketBuffer(audio_decoder->buffer, audio_decoder->out_frame)) {
        av_frame_unref(audio_decoder->out_frame);
    }
}

/**
 * Setup correct settings for the output frame, these are required by swr_convert_frame.
 */
static void prepare_out_frame(const Kit_AudioDecoder *audio_decoder) {
    Kit_FindAVChannelLayout(audio_decoder->output.channels, &audio_decoder->out_frame->ch_layout);
    audio_decoder->out_frame->format = Kit_FindAVSampleFormat(audio_decoder->output.format);
    audio_decoder->out_frame->sample_rate = audio_decoder->output.sample_rate;
}

/**
 * Process the input frame, and write the contents to the FIFO buffer. Note that this should be relatively efficient,
 * as the fifo will just grab the buffer from the frame to its internal frame list.
 */
static void process_decoded_frame(Kit_AudioDecoder *audio_decoder) {
    prepare_out_frame(audio_decoder);
    swr_convert_frame(audio_decoder->swr, audio_decoder->out_frame, audio_decoder->in_frame);
    av_audio_fifo_write(
        audio_decoder->fifo, (void **)audio_decoder->out_frame->data, audio_decoder->out_frame->nb_samples
    );
    if(audio_decoder->fifo_start_pts == -1)
        audio_decoder->fifo_start_pts = audio_decoder->in_frame->best_effort_timestamp;
    av_frame_unref(audio_decoder->out_frame);
}

/**
 * This is used to ramp up buffer size limit. When frame output is (almost) empty, we want to quickly get data in
 * for consumption. When frame output is more full, we want to use larger buffers for efficiency.
 */
static int get_limit(Kit_AudioDecoder *audio_decoder) {
    size_t size = Kit_GetPacketBufferLength(audio_decoder->buffer);
    if(size < 16)
        return 0;
    return pow(2, min2(size - 8, 13));
}

/**
 * If there is enough data in the FIFO, flush it out as a packet. This will do a memory copy!
 */
static void read_fifo_frame(Kit_AudioDecoder *audio_decoder, bool flush) {
    int fifo_samples = av_audio_fifo_size(audio_decoder->fifo);
    int nb_sample_limit = get_limit(audio_decoder);
    if(fifo_samples > nb_sample_limit || flush) {
        // Setup an appropriately sized AVFrame
        prepare_out_frame(audio_decoder);
        audio_decoder->out_frame->nb_samples = fifo_samples;
        audio_decoder->out_frame->best_effort_timestamp = audio_decoder->fifo_start_pts;
        av_frame_get_buffer(audio_decoder->out_frame, 0);

        // Read all available data from the FIFO
        av_audio_fifo_read(audio_decoder->fifo, (void **)audio_decoder->out_frame->data, fifo_samples);
        audio_decoder->fifo_start_pts = -1;
        write_packet(audio_decoder, fifo_samples);
    }
}

static void dec_flush_audio_cb(Kit_Decoder *decoder) {
    assert(decoder);
    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    Kit_FlushPacketBuffer(audio_decoder->buffer);
    av_audio_fifo_reset(audio_decoder->fifo);
    audio_decoder->fifo_start_pts = -1;
}

static void dec_signal_audio_cb(Kit_Decoder *decoder) {
    assert(decoder);
    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    Kit_SignalPacketBuffer(audio_decoder->buffer);
}

static void dec_get_audio_buffers_cb(const Kit_Decoder *ref, unsigned int *length, unsigned int *capacity) {
    assert(ref);
    assert(ref->userdata);
    Kit_AudioDecoder *audio_decoder = ref->userdata;
    if(length != NULL)
        *length = Kit_GetPacketBufferLength(audio_decoder->buffer);
    if(capacity != NULL)
        *capacity = Kit_GetPacketBufferCapacity(audio_decoder->buffer);
}

static Kit_DecoderInputResult dec_input_audio_cb(const Kit_Decoder *decoder, const AVPacket *in_packet) {
    assert(decoder != NULL);
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

static bool dec_decode_audio_cb(const Kit_Decoder *decoder, double *pts) {
    assert(decoder != NULL);
    int ret;

    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    ret = avcodec_receive_frame(decoder->codec_ctx, audio_decoder->in_frame);
    if(ret == 0) {
        *pts = audio_decoder->in_frame->best_effort_timestamp * av_q2d(decoder->stream->time_base);
        process_decoded_frame(audio_decoder);
        read_fifo_frame(audio_decoder, false);
        av_frame_unref(audio_decoder->in_frame);
        return true;
    }
    if(ret == AVERROR_EOF) {
        // If this is the end of the stream, flush the FIFO.
        read_fifo_frame(audio_decoder, true);
    }
    return false;
}

static void dec_close_audio_cb(Kit_Decoder *ref) {
    if(ref == NULL)
        return;
    assert(ref->userdata);
    Kit_AudioDecoder *audio_dec = ref->userdata;
    av_frame_free(&audio_dec->in_frame);
    av_frame_free(&audio_dec->out_frame);
    av_frame_free(&audio_dec->current);
    av_audio_fifo_free(audio_dec->fifo);
    swr_free(&audio_dec->swr);
    Kit_FreePacketBuffer(&audio_dec->buffer);
    free(audio_dec);
}

Kit_Decoder *Kit_CreateAudioDecoder(
    const Kit_Source *src, const Kit_AudioFormatRequest *format_request, Kit_Timer *sync_timer, const int stream_index
) {
    assert(src != NULL);

    const Kit_LibraryState *state = Kit_GetLibraryState();
    const AVFormatContext *format_ctx = src->format_ctx;
    Kit_Decoder *decoder = NULL;
    Kit_AudioDecoder *audio_decoder = NULL;
    Kit_PacketBuffer *buffer = NULL;
    AVFrame *in_frame = NULL;
    AVFrame *out_frame = NULL;
    AVFrame *current = NULL;
    AVChannelLayout out_layout;
    AVStream *stream = NULL;
    AVAudioFifo *fifo = NULL;
    SwrContext *swr = NULL;
    Kit_AudioOutputFormat output;

    // Find and set up stream.
    if(stream_index < 0 || stream_index >= format_ctx->nb_streams) {
        Kit_SetError("Invalid audio stream index %d", stream_index);
        return NULL;
    }
    stream = format_ctx->streams[stream_index];

    if((audio_decoder = calloc(1, sizeof(Kit_AudioDecoder))) == NULL) {
        Kit_SetError("Unable to allocate audio decoder for stream %d", stream_index);
        goto exit_none;
    }
    if((decoder = Kit_CreateDecoder(
            stream,
            sync_timer,
            state->thread_count,
            KIT_HWDEVICE_TYPE_ALL,
            dec_input_audio_cb,
            dec_decode_audio_cb,
            dec_flush_audio_cb,
            dec_signal_audio_cb,
            dec_close_audio_cb,
            dec_get_audio_buffers_cb,
            audio_decoder
        )) == NULL) {
        // No need to Kit_SetError, it will be set in Kit_CreateDecoder.
        goto exit_audio_dec;
    }
    if((in_frame = av_frame_alloc()) == NULL) {
        Kit_SetError("Unable to allocate input audio frame for stream %d", stream_index);
        goto exit_decoder;
    }
    if((out_frame = av_frame_alloc()) == NULL) {
        Kit_SetError("Unable to allocate output audio frame for stream %d", stream_index);
        goto exit_in_frame;
    }
    if((current = av_frame_alloc()) == NULL) {
        Kit_SetError("Unable to allocate output audio frame for stream %d", stream_index);
        goto exit_out_frame;
    }
    if((buffer = Kit_CreatePacketBuffer(
            state->audio_frame_buffer_size,
            (buf_obj_alloc)av_frame_alloc,
            (buf_obj_unref)av_frame_unref,
            (buf_obj_free)av_frame_free,
            (buf_obj_move)av_frame_move_ref,
            (buf_obj_ref)av_frame_ref
        )) == NULL) {
        Kit_SetError("Unable to create an output buffer for stream %d", stream_index);
        goto exit_current;
    }

    memset(&output, 0, sizeof(Kit_AudioOutputFormat));
    output.sample_rate =
        (format_request->sample_rate > -1) ? format_request->sample_rate : decoder->codec_ctx->sample_rate;
    output.channels = (format_request->channels > -1) ? format_request->channels
                                                      : Kit_FindChannelLayout(&decoder->codec_ctx->ch_layout);
    output.bytes =
        (format_request->bytes > -1) ? format_request->bytes : Kit_FindBytes(decoder->codec_ctx->sample_fmt);
    output.is_signed = (format_request->is_signed > -1) ? format_request->is_signed
                                                        : Kit_FindSignedness(decoder->codec_ctx->sample_fmt);
    output.format = (format_request->format != 0) ? format_request->format
                                                  : Kit_FindSDLSampleFormat(decoder->codec_ctx->sample_fmt);

    Kit_FindAVChannelLayout(output.channels, &out_layout);
    if(swr_alloc_set_opts2(
           &swr,
           &out_layout,
           Kit_FindAVSampleFormat(output.format),
           output.sample_rate,
           &decoder->codec_ctx->ch_layout,
           decoder->codec_ctx->sample_fmt,
           decoder->codec_ctx->sample_rate,
           0,
           NULL
       ) != 0) {
        Kit_SetError("Unable to allocate audio resampler context");
        goto exit_buffer;
    }
    if(swr_init(swr) != 0) {
        Kit_SetError("Unable to initialize audio resampler context");
        goto exit_swr;
    }
    fifo = av_audio_fifo_alloc(Kit_FindAVSampleFormat(output.format), out_layout.nb_channels, 1024 * 16);
    if(fifo == NULL) {
        Kit_SetError("Unable to allocate audio FIFO for stream %d", stream_index);
        goto exit_swr;
    }

    audio_decoder->current = current;
    audio_decoder->in_frame = in_frame;
    audio_decoder->out_frame = out_frame;
    audio_decoder->swr = swr;
    audio_decoder->buffer = buffer;
    audio_decoder->output = output;
    audio_decoder->fifo = fifo;
    audio_decoder->fifo_start_pts = -1;
    return decoder;

exit_swr:
    swr_free(&swr);
exit_buffer:
    Kit_FreePacketBuffer(&buffer);
exit_current:
    av_frame_free(&current);
exit_out_frame:
    av_frame_free(&out_frame);
exit_in_frame:
    av_frame_free(&in_frame);
exit_decoder:
    Kit_CloseDecoder(&decoder);
    return NULL; // Above frees the audio_decoder also.
exit_audio_dec:
    free(audio_decoder);
exit_none:
    return NULL;
}

static double Kit_GetCurrentPTS(const Kit_Decoder *decoder) {
    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    return audio_decoder->current->best_effort_timestamp * av_q2d(decoder->stream->time_base);
}

int Kit_GetAudioDecoderData(Kit_Decoder *decoder, size_t backend_buffer_size, unsigned char *buf, size_t len) {
    assert(decoder != NULL);

    const Kit_AudioDecoder *audio_decoder = decoder->userdata;
    int ret = 0;
    size_t *size;
    size_t *left;

    if(len <= 0)
        return 0;
    if(!Kit_BeginPacketBufferRead(audio_decoder->buffer, audio_decoder->current, 0))
        goto no_data;

    // Initialize timer if it's the primary sync source, and it's not yet initialized.
    Kit_InitTimerBase(decoder->sync_timer);
    if(!Kit_IsTimerInitialized(decoder->sync_timer)) {
        // If this was not the sync source and timer is not set, wait for another stream to set it.
        av_frame_unref(audio_decoder->current);
        Kit_CancelPacketBufferRead(audio_decoder->buffer);
        return 0;
    }

    double pts = Kit_GetCurrentPTS(decoder);
    double sync_ts = Kit_GetTimerElapsed(decoder->sync_timer);
    const double early_threshold = Kit_GetLibraryState()->audio_early_threshold / 1000.0;
    const double late_threshold = Kit_GetLibraryState()->audio_late_threshold / 1000.0;

    // If packet is far too early, the stream jumped or was seeked.
    if(Kit_IsTimerPrimary(decoder->sync_timer)) {
        // If this stream is the sync source, then reset this as the new sync timestamp.
        if(pts > sync_ts + KIT_AUDIO_EARLY_FAIL) {
            // LOG("[AUDIO] NO SYNC pts = %lf > %lf + %lf\n", pts, sync_ts, KIT_AUDIO_EARLY_FAIL);
            // LOG("[AUDIO] Adjusting by = %lf\n", -(pts - sync_ts));
            Kit_AddTimerBase(decoder->sync_timer, -(pts - sync_ts));
            sync_ts = Kit_GetTimerElapsed(decoder->sync_timer);
        }
    } else {
        // If this stream is NOT the sync source, try to skip packets until we see something reasonable.
        while(pts > sync_ts + KIT_AUDIO_EARLY_FAIL) {
            // LOG("[AUDIO] FAIL-EARLY: pts = %lf < %lf + %lf\n", pts, sync_ts, KIT_AUDIO_EARLY_FAIL);
            av_frame_unref(audio_decoder->current);
            Kit_FinishPacketBufferRead(audio_decoder->buffer);
            if(!Kit_BeginPacketBufferRead(audio_decoder->buffer, audio_decoder->current, 0))
                goto no_data;
            pts = Kit_GetCurrentPTS(decoder);
        }
    }

    // Packet is too early, wait.
    if(pts > sync_ts + early_threshold) {
        // LOG("[AUDIO] EARLY pts = %lf > %lf + %lf\n", pts, sync_ts, early_threshold);
        av_frame_unref(audio_decoder->current);
        Kit_CancelPacketBufferRead(audio_decoder->buffer);
        goto no_data;
    }

    // Packet is too late, skip packets until we see something reasonable.
    while(pts < sync_ts - late_threshold) {
        // LOG("[AUDIO] LATE: pts = %lf < %lf - %lf\n", pts, sync_ts, late_threshold);
        av_frame_unref(audio_decoder->current);
        Kit_FinishPacketBufferRead(audio_decoder->buffer);
        if(!Kit_BeginPacketBufferRead(audio_decoder->buffer, audio_decoder->current, 0))
            goto no_data;
        pts = Kit_GetCurrentPTS(decoder);
    }
    // LOG("[AUDIO] >>> SYNC!: pts = %lf, sync = %lf\n", pts, sync_ts);

    size = &audio_decoder->current->crop_top;
    left = &audio_decoder->current->crop_bottom;
    len = floor(len / SAMPLE_BYTES(audio_decoder)) * SAMPLE_BYTES(audio_decoder);
    if(*left) {
        ret = (len > *left) ? *left : len;
        int pos = *size - *left;
        memcpy(buf, audio_decoder->current->data[0] + pos, ret);
        *left -= ret;
    }

    av_frame_unref(audio_decoder->current);
    if(*left) {
        Kit_CancelPacketBufferRead(audio_decoder->buffer);
    } else {
        Kit_FinishPacketBufferRead(audio_decoder->buffer);
    }
    return ret;

no_data:
    len = min2(floor(len / SAMPLE_BYTES(audio_decoder)), 1024);
    if(backend_buffer_size < len * SAMPLE_BYTES(audio_decoder)) {
        // LOG("[AUDIO] SILENCE due to backend size %ld < %ld\n", backend_buffer_size, len *
        // SAMPLE_BYTES(audio_decoder));
        av_samples_set_silence(
            &buf, 0, len, audio_decoder->output.channels, Kit_FindAVSampleFormat(audio_decoder->output.format)
        );
        return len * SAMPLE_BYTES(audio_decoder);
    }
    return 0;
}
