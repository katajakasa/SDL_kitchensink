#include <assert.h>
#include <inttypes.h>

#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <SDL.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/audio/kitaudio.h"
#include "kitchensink/internal/audio/kitaudioutils.h"

#define KIT_AUDIO_SYNC_THRESHOLD 0.05

typedef struct Kit_AudioDecoder {
    SwrContext *swr;                 ///< Audio resampler context
    AVFrame *in_frame;               ///< Temporary AVFrame for audio decoding purposes
    AVFrame *out_frame;              ///< Temporary AVFrame fur audio resampling purposes
    AVFrame *current;                ///< Audio packet we are currently reading from
    int left;                        ///< How much data we have left in the current packet
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

    // Write audio packet to packet buffer. This may block!
    // No need to av_packet_unref, since Kit_WritePacketBuffer will move the refs.
    Kit_WritePacketBuffer(audio_decoder->buffer, audio_decoder->out_frame);
}

static void dec_flush_audio_cb(Kit_Decoder *decoder) {
    assert(decoder);
    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    Kit_FlushPacketBuffer(audio_decoder->buffer);
    av_frame_unref(audio_decoder->current);
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

static bool dec_decode_audio_cb(const Kit_Decoder *dec) {
    assert(dec != NULL);
    Kit_AudioDecoder *audio_decoder = dec->userdata;

    if(avcodec_receive_frame(dec->codec_ctx, audio_decoder->in_frame) == 0) {
        dec_read_audio(dec);
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
    if(audio_dec->in_frame != NULL)
        av_frame_free(&audio_dec->in_frame);
    if(audio_dec->out_frame != NULL)
        av_frame_free(&audio_dec->out_frame);
    if(audio_dec->current != NULL)
        av_frame_free(&audio_dec->current);
    if(audio_dec->swr != NULL)
        swr_free(&audio_dec->swr);
    if(audio_dec->buffer != NULL)
        Kit_FreePacketBuffer(&audio_dec->buffer);
    free(audio_dec);
}

Kit_Decoder* Kit_CreateAudioDecoder(const Kit_Source *src, int stream_index) {
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
        (buf_obj_move) av_frame_move_ref)) == NULL) {
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
    audio_decoder->left = 0;
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

#define PACKET_SIZE(audio_decoder) (audio_decoder->current->nb_samples * audio_decoder->output.channels * audio_decoder->output.bytes)

/**
 * Get a new packet from the audio decoder output and bump it to the current packet slot.
 */
static bool Kit_PopAudioPacket(Kit_Decoder *decoder) {
    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    av_frame_unref(audio_decoder->current);
    if(!Kit_ReadPacketBuffer(audio_decoder->buffer, audio_decoder->current, 0)) {
        return false;
    }
    audio_decoder->left = PACKET_SIZE(audio_decoder);
    return true;
}

double Kit_GetClipTime(const Kit_AudioOutputFormat *output, size_t bytes) {
    int bytes_per_sample = output->bytes * output->channels;
    double bytes_per_second = bytes_per_sample * output->sample_rate;
    return ((double)bytes) / bytes_per_second;
}

int Kit_GetAudioDecoderData(Kit_Decoder *decoder, unsigned char *buf, int len) {
    assert(decoder != NULL);
    Kit_AudioDecoder *audio_decoder = decoder->userdata;
    int pos, ret = 0;
    double pts;
    double sync_ts;

    // Immediately bail if nothing is requested.
    if(len <= 0)
        return 0;

    // If we have no data left in current buffer, try to get some more from the decoder output.
    if(audio_decoder->left <= 0)
        if(!Kit_PopAudioPacket(decoder))
            return 0;

    // Get the presentation timestamp of the current frame, and set the sync clock if it was not yet set.
    pts = Kit_GetCurrentPTS(decoder);
    if(decoder->clock_sync < 0) {
        decoder->clock_sync = Kit_GetSystemTime() + pts;
    }

    // If packet should not yet be played, stop here and wait.
    // If packet should have already been played, skip it and try to find a better packet.
    sync_ts = Kit_GetSystemTime() - decoder->clock_sync;
    if(pts > sync_ts + KIT_AUDIO_SYNC_THRESHOLD) {
        return 0;
    }
    while(pts < sync_ts - KIT_AUDIO_SYNC_THRESHOLD) {
        if(!Kit_PopAudioPacket(decoder))
            return 0;
        pts = Kit_GetCurrentPTS(decoder);
    }

    decoder->clock_pos = pts;
    if(audio_decoder->left) {
        ret = (len > audio_decoder->left) ? audio_decoder->left : len;
        pos = PACKET_SIZE(audio_decoder) - audio_decoder->left;
        memcpy(buf, audio_decoder->current->data[0] + pos, ret);
        audio_decoder->left -= ret;
        decoder->clock_pos += Kit_GetClipTime(&audio_decoder->output, pos);
    }

    return ret;
}
