#include <assert.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <libavformat/avformat.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <SDL.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitlibstate.h"
#include "kitchensink/internal/utils/kithelpers.h"
#include "kitchensink/internal/utils/kitlog.h"
#include "kitchensink/internal/audio/kitaudio.h"
#include "kitchensink/internal/audio/kitaudiopacket.h"
#include "kitchensink/internal/audio/kitaudioutils.h"

#define KIT_AUDIO_SYNC_THRESHOLD 0.05

typedef struct Kit_AudioDecoder {
    SwrContext *swr;                 ///< Audio resampler context
    AVFrame *scratch_frame;          ///< Temporary AVFrame for audio decoding purposes
    Kit_AudioPacket *scratch_packet; ///< Temporary audio packet for decoding purposes
    Kit_AudioPacket *current;        ///< Audio packet we are currently reading from
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
    const Kit_AudioDecoder *audio_dec = dec->userdata;
    int len;
    int dst_line_size;
    int dst_nb_samples;
    int dst_buf_size;
    double pts;
    unsigned char **dst_data;

    dst_nb_samples = av_rescale_rnd(
        audio_dec->scratch_frame->nb_samples,
        audio_dec->output.sample_rate,  // Target sample rate
        dec->codec_ctx->sample_rate,  // Source sample rate
        AV_ROUND_UP);

    av_samples_alloc_array_and_samples(
        &dst_data,
        &dst_line_size,
        audio_dec->output.channels,
        dst_nb_samples,
        Kit_FindAVSampleFormat(audio_dec->output.format),
        0);

    len = swr_convert(
        audio_dec->swr,
        dst_data,
        dst_nb_samples,
        (const unsigned char **)audio_dec->scratch_frame->extended_data,
        audio_dec->scratch_frame->nb_samples);

    dst_buf_size = av_samples_get_buffer_size(
        &dst_line_size,
        audio_dec->output.channels,
        len,
        Kit_FindAVSampleFormat(audio_dec->output.format), 1);

    // Get presentation timestamp
    pts = audio_dec->scratch_frame->best_effort_timestamp * av_q2d(dec->stream->time_base);

    // Write audio packet to packet buffer. This may block! Free any unnecessary resources after.
    Kit_SetAudioPacketData(audio_dec->scratch_packet, dst_data[0], dst_buf_size, pts);
    Kit_WritePacketBuffer(audio_dec->buffer, audio_dec->scratch_packet);
    av_freep(&dst_data);
}


static bool dec_input_audio_cb(const Kit_Decoder *dec, const AVPacket *in_packet) {
    assert(dec != NULL);
    assert(in_packet != NULL);
    return avcodec_send_packet(dec->codec_ctx, in_packet) < 0;
}

static bool dec_decode_audio_cb(const Kit_Decoder *dec) {
    assert(dec != NULL);
    Kit_AudioDecoder *audio_decoder = dec->userdata;

    if(avcodec_receive_frame(dec->codec_ctx, audio_decoder->scratch_frame) == 0) {
        dec_read_audio(dec);
        av_frame_unref(audio_decoder->scratch_frame);
        return true;
    }

    return false;
}

static void dec_close_audio_cb(Kit_Decoder *ref) {
    if(ref == NULL)
        return;
    assert(ref->userdata);
    Kit_AudioDecoder *audio_dec = ref->userdata;
    if(audio_dec->current != NULL)
        Kit_FreeAudioPacket(&audio_dec->current);
    if(audio_dec->scratch_packet != NULL)
        Kit_FreeAudioPacket(&audio_dec->scratch_packet);
    if(audio_dec->scratch_frame != NULL)
        av_frame_free(&audio_dec->scratch_frame);
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
    AVFrame *scratch_frame = NULL;
    AVChannelLayout layout;
    AVStream *stream = NULL;
    SwrContext *swr = NULL;
    Kit_AudioOutputFormat output;
    Kit_AudioPacket *current = NULL;
    Kit_AudioPacket *scratch_packet = NULL;

    // Find and set up stream.
    if(stream_index < 0 || stream_index >= format_ctx->nb_streams) {
        Kit_SetError("Invalid audio stream index %d", stream_index);
        return NULL;
    }
    stream = format_ctx->streams[stream_index];

    if((audio_decoder = calloc(1, sizeof(Kit_AudioDecoder))) == NULL) {
        Kit_SetError("Unable to allocate audio decoder for stream %d", stream_index);
        goto exit_0;
    }
    if((decoder = Kit_CreateDecoder(
            stream,
            state->thread_count,
            dec_input_audio_cb,
            dec_decode_audio_cb,
            dec_close_audio_cb,
            audio_decoder)) == NULL) {
        // No need to Kit_SetError, it will be set in Kit_CreateDecoder.
        goto exit_1;
    }
    if((scratch_frame = av_frame_alloc()) == NULL) {
        Kit_SetError("Unable to allocate temporary audio frame for stream %d", stream_index);
        goto exit_2;
    }
    if((buffer = Kit_CreatePacketBuffer(
        64,
        (buf_obj_alloc) Kit_CreateAudioPacket,
        (buf_obj_unref) Kit_DelAudioPacketRefs,
        (buf_obj_free) Kit_FreeAudioPacket,
        (buf_obj_move) Kit_MoveAudioPacketRefs)) == NULL) {
        Kit_SetError("Unable to create an output buffer for stream %d", stream_index);
        goto exit_3;
    }
    if((current = Kit_CreateAudioPacket()) == NULL) {
        Kit_SetError("Unable to allocate reader audio packet for stream %d", stream_index);
        goto exit_4;
    }
    if((scratch_packet = Kit_CreateAudioPacket()) == NULL) {
        Kit_SetError("Unable to allocate scratch audio packet for stream %d", stream_index);
        goto exit_5;
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
            &layout, // Target channel layout
            Kit_FindAVSampleFormat(output.format), // Target fmt
            output.sample_rate, // Target sample rate
            &decoder->codec_ctx->ch_layout, // Source channel layout
            decoder->codec_ctx->sample_fmt, // Source fmt
            decoder->codec_ctx->sample_rate, // Source sample rate
            0,
            NULL) != 0) {
        Kit_SetError("Unable to allocate audio resampler context");
        goto exit_6;
    }
    if(swr_init(swr) != 0) {
        Kit_SetError("Unable to initialize audio resampler context");
        goto exit_7;
    }

    audio_decoder->current = current;
    audio_decoder->scratch_frame = scratch_frame;
    audio_decoder->scratch_packet = scratch_packet;
    audio_decoder->swr = swr;
    audio_decoder->buffer = buffer;
    audio_decoder->output = output;
    return decoder;

exit_7:
    swr_free(&swr);
exit_6:
    Kit_FreeAudioPacket(&scratch_packet);
exit_5:
    Kit_FreeAudioPacket(&current);
exit_4:
    Kit_FreePacketBuffer(&buffer);
exit_3:
    av_frame_free(&scratch_frame);
exit_2:
    Kit_CloseDecoder(&decoder);
exit_1:
    free(audio_decoder);
exit_0:
    return NULL;
}

/**
 * Get a new packet from the audio decoder output and bump it to the current packet slot.
 */
bool Kit_PopAudioPacket(const Kit_AudioDecoder *audio_decoder) {
    Kit_DelAudioPacketRefs(audio_decoder->current);
    if(!Kit_ReadPacketBuffer(audio_decoder->buffer, audio_decoder->current, 0)) {
        return false;
    }
    return true;
}

/**
 * Calculate the duration (in seconds) of a clip of audio stream.
 */
double Kit_GetClipTime(const Kit_AudioOutputFormat *output, size_t bytes) {
    int bytes_per_sample = output->bytes * output->channels;
    double bytes_per_second = bytes_per_sample * output->sample_rate;
    return ((double)bytes) / bytes_per_second;
}

int Kit_GetAudioDecoderData(Kit_Decoder *decoder, unsigned char *buf, int len) {
    assert(decoder != NULL);
    const Kit_AudioDecoder *audio_decoder = decoder->userdata;
    Kit_AudioPacket *current = audio_decoder->current;
    int ret = 0;
    int pos;
    double sync_ts;

    // If we have no data left in current buffer, try to get some more from the decoder output.
    if(current->left <= 0)
        if(!Kit_PopAudioPacket(audio_decoder))
            return 0;

    // If packet should not yet be played, stop here and wait.
    // If packet should have already been played, skip it and try to find a better packet.
    // For audio, it is possible that we cannot find good packet. Then just don't read anything.
    sync_ts = Kit_GetSystemTime() - decoder->clock_sync;
    if(current->pts > sync_ts + KIT_AUDIO_SYNC_THRESHOLD) {
        return 0;
    }
    while(current->pts < sync_ts - KIT_AUDIO_SYNC_THRESHOLD) {
        if(!Kit_PopAudioPacket(audio_decoder))
            return 0;
    }

    // Read data from packet ringbuffer
    decoder->clock_pos = current->pts;
    if(current->left) {
        ret = len > current->left ? current->left : len;
        pos = current->length - current->left;
        memcpy(buf, current->data + pos, ret);
        current->pts += Kit_GetClipTime(&audio_decoder->output, ret);
        current->left -= ret;
    }

    return ret;
}
