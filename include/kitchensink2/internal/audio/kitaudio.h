#ifndef KITAUDIO_H
#define KITAUDIO_H

/**
 * @brief Internal audio decoder: decodes, resamples and synchronizes an audio stream.
 *
 * @file kitaudio.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/internal/kitdecoder.h"
#include "kitchensink2/internal/kittimer.h"
#include "kitchensink2/kitconfig.h"
#include "kitchensink2/kitformat.h"
#include "kitchensink2/kitsource.h"

/**
 * @brief Creates and initializes an audio decoder for the given stream.
 *
 * Sets up the resampler, output FIFO and packet buffer for the selected stream, converting
 * to the format described by format_request (falling back to source-derived defaults for any
 * field left unset). On failure, the decoder is destroyed and NULL is returned; in all cases
 * ownership of sync_timer is taken by this call, even on failure.
 *
 * @param src Source to read the stream from
 * @param format_request Requested output audio format, or defaults where fields are unset
 * @param sync_timer Sync timer to attach to the decoder (ownership transferred to this call)
 * @param stream_index Index of the audio stream to decode
 * @return New decoder on success, NULL on error (see Kit_GetError())
 */
KIT_LOCAL Kit_Decoder *Kit_CreateAudioDecoder(
    const Kit_Source *src, const Kit_AudioFormatRequest *format_request, Kit_Timer *sync_timer, int stream_index
);

/**
 * @brief Reads synchronized, decoded audio data out of the decoder.
 *
 * Pulls the next buffered frame, discards frames whose seek serial does not match the sync timer,
 * and skips/waits frames based on presentation timestamp versus the sync clock. If no frame is
 * currently deliverable and the codec has not yet reached end of stream (see the decoder's
 * eof_seen flag), silence is synthesized instead -- but only when backend_buffer_size indicates
 * the backend queue is running low, to avoid audible underruns. Once codec-level EOF has been
 * seen, this returns 0 instead of generating silence.
 *
 * @param dec Audio decoder instance
 * @param backend_buffer_size Amount of data currently queued in the playback backend, in bytes
 * @param buf Buffer to write decoded (or silence) data into
 * @param len Maximum number of bytes to write into buf
 * @return Number of bytes written (may be less than len, or 0 if nothing was available)
 */
KIT_LOCAL int Kit_GetAudioDecoderData(Kit_Decoder *dec, size_t backend_buffer_size, unsigned char *buf, size_t len);

/**
 * @brief Retrieves the negotiated output audio format for a decoder.
 *
 * If dec is NULL, output is zeroed and 1 is returned instead of failing.
 *
 * @param dec Audio decoder instance, or NULL
 * @param output Output format struct to fill in
 * @return 0 on success, 1 if dec was NULL
 */
KIT_LOCAL int Kit_GetAudioDecoderOutputFormat(const Kit_Decoder *dec, Kit_AudioOutputFormat *output);

#endif // KITAUDIO_H
