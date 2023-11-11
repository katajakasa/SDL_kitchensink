#include <assert.h>
#include <inttypes.h>

#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <SDL.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/utils/kitlog.h"
#include "kitchensink/internal/audio/kitaudio.h"
#include "kitchensink/internal/audio/kitaudioutils.h"

#define KIT_AUDIO_EARLY_FAIL 5
#define KIT_AUDIO_EARLY_THRESHOLD 0.01
#define KIT_AUDIO_LATE_THRESHOLD 0.05

#define SAMPLE_BYTES(audio_decoder) (audio_decoder->output.channels * audio_decoder->output.bytes)

typedef struct Kit_AudioDecoder {
    SwrContext *swr;                 ///< Audio resampler context
    AVFrame *in_frame;               ///< Temporary AVFrame for audio decoding purposes
    AVFrame *out_frame;              ///< Temporary AVFrame fur audio resampling purposes
    AVFrame *current;                ///< Audio packet we are currently reading from
    Kit_PacketBuffer *buffer;        ///< Packet ringbuffer for decoded audio packets
    Kit_AudioOutputFormat output;    ///< Output audio format description
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

static void dec_read_audio(const Kit_Decoder *dec) {
    const Kit_AudioDecoder *audio_decoder = dec->userdata;

    // Setup correct settings for the output frame, these are required by swr_convert_frame.
    Kit_FindAVChannelLayout(audio_decoder->output.channels, &audio_decoder->out_frame->ch_layout);
    audio_decoder->out_frame->format = Kit_FindAVSampleFormat(audio_decoder->output.format);
    audio_decoder->out_frame->sample_rate = audio_decoder->output.sample_rate;

    // Convert frame and copy props to the new frame we got.
    swr_convert_frame(audio_decoder->swr, audio_decoder->out_frame, audio_decoder->in_frame);
    av_frame_copy_props(audio_decoder->out_frame, audio_decoder->in_frame);

    // Save bytes left in the frame. Misuse crop values, since they are only used for video packets by ffmpeg.
    audio_decoder->out_frame->crop_top = SAMPLE_BYTES(audio_decoder) * audio_decoder->out_frame->nb_samples;
    audio_decoder->out_frame->crop_bottom = audio_decoder->out_frame->crop_top;

    // Write video packet to packet buffer. This may block!
    // if write succeeds, no need to av_packet_unref, since Kit_WritePacketBuffer will move the refs.
    // If write fails, unref the packet. Fails should only happen if we are closing or seeking, so it is fine.
    if (!Kit_WritePacketBuffer(audio_decoder->buffer, audio_decoder->out_frame)) {
        av_frame_unref(audio_decoder->out_frame);
    }
}

static void dec_flush_audio_cb(Kit_Decoder *decoder) {
    assert(decoder);
    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    Kit_FlushPacketBuffer(audio_decoder->buffer);
}

static void dec_signal_audio_cb(Kit_Decoder *decoder) {
    assert(decoder);
    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    Kit_SignalPacketBuffer(audio_decoder->buffer);
}

static bool dec_input_audio_cb(const Kit_Decoder *dec, const AVPacket *in_packet) {
    assert(dec != NULL);
    assert(in_packet != NULL);
    return avcodec_send_packet(dec->codec_ctx, in_packet) == 0;
}

static bool dec_decode_audio_cb(const Kit_Decoder *decoder, double *pts) {
    assert(decoder != NULL);
    Kit_AudioDecoder *audio_decoder = decoder->userdata;

    if(avcodec_receive_frame(decoder->codec_ctx, audio_decoder->in_frame) == 0) {
        *pts = audio_decoder->in_frame->best_effort_timestamp * av_q2d(decoder->stream->time_base);
        dec_read_audio(decoder);
        av_frame_unref(audio_decoder->in_frame);
        return true;
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
    swr_free(&audio_dec->swr);
    Kit_FreePacketBuffer(&audio_dec->buffer);
    free(audio_dec);
}

Kit_Decoder* Kit_CreateAudioDecoder(const Kit_Source *src, Kit_Timer *sync_timer, int stream_index) {
    assert(src != NULL);

    const Kit_LibraryState *state = Kit_GetLibraryState();
    const AVFormatContext *format_ctx = src->format_ctx;
    Kit_Decoder *decoder = NULL;
    Kit_AudioDecoder *audio_decoder = NULL;
    Kit_PacketBuffer *buffer = NULL;
    AVFrame *in_frame = NULL;
    AVFrame *out_frame = NULL;
    AVFrame *current = NULL;
    AVChannelLayout layout;
    AVStream *stream = NULL;
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
            dec_input_audio_cb,
            dec_decode_audio_cb,
            dec_flush_audio_cb,
            dec_signal_audio_cb,
            dec_close_audio_cb,
            audio_decoder)) == NULL) {
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
        8,
        (buf_obj_alloc) av_frame_alloc,
        (buf_obj_unref) av_frame_unref,
        (buf_obj_free) av_frame_free,
        (buf_obj_move) av_frame_move_ref,
        (buf_obj_ref)  av_frame_ref)) == NULL) {
        Kit_SetError("Unable to create an output buffer for stream %d", stream_index);
        goto exit_current;
    }

    memset(&output, 0, sizeof(Kit_AudioOutputFormat));
    output.sample_rate = decoder->codec_ctx->sample_rate;
    output.channels = Kit_FindChannelLayout(&decoder->codec_ctx->ch_layout);
    output.bytes = Kit_FindBytes(decoder->codec_ctx->sample_fmt);
    output.is_signed = Kit_FindSignedness(decoder->codec_ctx->sample_fmt);
    output.format = Kit_FindSDLSampleFormat(decoder->codec_ctx->sample_fmt);

    Kit_FindAVChannelLayout(output.channels, &layout);
    if (swr_alloc_set_opts2(
            &swr,
            &layout,
            Kit_FindAVSampleFormat(output.format),
            output.sample_rate,
            &decoder->codec_ctx->ch_layout,
            decoder->codec_ctx->sample_fmt,
            decoder->codec_ctx->sample_rate,
            0,
            NULL) != 0) {
        Kit_SetError("Unable to allocate audio resampler context");
        goto exit_buffer;
    }
    if(swr_init(swr) != 0) {
        Kit_SetError("Unable to initialize audio resampler context");
        goto exit_swr;
    }

    audio_decoder->current = current;
    audio_decoder->in_frame = in_frame;
    audio_decoder->out_frame = out_frame;
    audio_decoder->swr = swr;
    audio_decoder->buffer = buffer;
    audio_decoder->output = output;
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
exit_audio_dec:
    free(audio_decoder);
exit_none:
    return NULL;
}

static double Kit_GetCurrentPTS(const Kit_Decoder *decoder) {
    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    return audio_decoder->current->best_effort_timestamp * av_q2d(decoder->stream->time_base);
}

int Kit_GetAudioDecoderData(Kit_Decoder *decoder, unsigned char *buf, size_t len) {
    assert(decoder != NULL);

    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    int pos;
    int ret = 0;
    size_t *size;
    size_t *left;
    double sync_ts;

    if(len <= 0)
        return 0;
    if(!Kit_BeginPacketBufferRead(audio_decoder->buffer, audio_decoder->current, 0))
        return 0;

    // If packet should not yet be played, stop here and wait.
    // If packet should have already been played, skip it and try to find a better packet.
    decoder->clock_pos = Kit_GetCurrentPTS(decoder);
    sync_ts = Kit_GetTimerElapsed(decoder->sync_timer);

    // If packet is far too early, the stream jumped or was seeked. Skip packets until we something valid.
    while(decoder->clock_pos > sync_ts + KIT_AUDIO_EARLY_FAIL) {
        //LOG("[AUDIO] FAIL-EARLY: pts = %lf < %lf + %lf\n", decoder->clock_pos, sync_ts, KIT_AUDIO_LATE_THRESHOLD);
        av_frame_unref(audio_decoder->current);
        Kit_FinishPacketBufferRead(audio_decoder->buffer);
        if(!Kit_BeginPacketBufferRead(audio_decoder->buffer, audio_decoder->current, 0))
            return 0;
        decoder->clock_pos = Kit_GetCurrentPTS(decoder);
    }

    // Packet is too early, wait.
    if(decoder->clock_pos > sync_ts + KIT_AUDIO_EARLY_THRESHOLD) {
        //LOG("[AUDIO] EARLY pts = %lf > %lf + %lf\n", decoder->clock_pos, sync_ts, KIT_AUDIO_EARLY_THRESHOLD);
        av_frame_unref(audio_decoder->current);
        Kit_CancelPacketBufferRead(audio_decoder->buffer);
        return 0;
    }

    // Packet is too late, skip packets until we see something reasonable.
    while(decoder->clock_pos < sync_ts - KIT_AUDIO_LATE_THRESHOLD) {
        //LOG("[AUDIO] LATE: pts = %lf < %lf + %lf\n", decoder->clock_pos, sync_ts, KIT_AUDIO_LATE_THRESHOLD);
        av_frame_unref(audio_decoder->current);
        Kit_FinishPacketBufferRead(audio_decoder->buffer);
        if(!Kit_BeginPacketBufferRead(audio_decoder->buffer, audio_decoder->current, 0))
            return 0;
        decoder->clock_pos = Kit_GetCurrentPTS(decoder);
    }
    //LOG("[AUDIO] >>> SYNC!: pts = %lf, sync = %lf\n", decoder->clock_pos, sync_ts);

    size = &audio_decoder->current->crop_top;
    left = &audio_decoder->current->crop_bottom;
    if(*left) {
        ret = (len > *left) ? *left : len;
        pos = *size - *left;
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
}
