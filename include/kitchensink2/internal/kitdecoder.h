#ifndef KITDECODER_H
#define KITDECODER_H

/**
 * @brief Generic libavcodec decoder wrapper shared by the video, audio and subtitle decoders. Owns the AVCodecContext,
 * handles hardware decoder negotiation, and dispatches decode/input/flush/abort/close work to per-type callbacks.
 *
 * @file kitdecoder.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <stdbool.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "kitchensink2/kitcodec.h"
#include "kitchensink2/kitconfig.h"
#include "kittimer.h"

/**
 * @brief Opaque generic decoder handle, shared by video, audio and subtitle decoding.
 */
typedef struct Kit_Decoder Kit_Decoder;

/**
 * @brief Result of feeding a single packet into a decoder's dec_input callback.
 */
typedef enum Kit_DecoderInputResult
{
    KIT_DEC_INPUT_OK = 0,   ///< Packet was accepted.
    KIT_DEC_INPUT_RETRY,    ///< Decoder's internal queue is full; caller should retry the same packet later.
    KIT_DEC_INPUT_EOF,      ///< Decoder has reached end of stream (e.g. avcodec_send_packet returned EOF).
} Kit_DecoderInputResult;

/** @brief Feeds one packet (or NULL to flush/drain) into the underlying codec. */
typedef Kit_DecoderInputResult (*dec_input_cb)(const Kit_Decoder *decoder, const AVPacket *packet);
/** @brief Pulls one decoded frame out of the underlying codec and stores its pts in @p pts. */
typedef bool (*dec_decode_cb)(const Kit_Decoder *decoder, double *pts);
/** @brief Discards any buffered/queued output frames for the decoder (e.g. after a seek). */
typedef void (*dec_flush_cb)(Kit_Decoder *decoder);
/** @brief Unblocks any output/input buffer waits so the owning thread can shut down. */
typedef void (*dec_abort_cb)(Kit_Decoder *decoder);
/** @brief Releases decoder-specific (userdata) resources; called before the codec context is freed. */
typedef void (*dec_close_cb)(Kit_Decoder *decoder);
/** @brief Reports current output buffer fill level and capacity for the decoder. */
typedef void (*dec_get_buffers_cb)(const Kit_Decoder *decoder, unsigned int *length, unsigned int *capacity);

/**
 * @brief Generic decoder state: libavcodec context plus type-specific callbacks and userdata.
 */
struct Kit_Decoder {
    Kit_Timer *sync_timer;              ///< Playback synchronization timer (also carries the seek serial)
    unsigned int output_serial;         ///< Serial stamped on decoded output frames. Only decoder thread touches this.
    AVRational aspect_ratio;            ///< Aspect ratio for the current frame (may change frame-to-frame)
    AVCodecContext *codec_ctx;          ///< FFMpeg internal: Codec context
    AVStream *stream;                   ///< FFMpeg internal: Data stream
    enum AVPixelFormat hw_fmt;          ///< FFMpeg internal: Hardware pixel format (if in use)
    enum AVHWDeviceType hw_type;        ///< FFMpeg internal: Hardware device type (if in use)
    void *userdata;                     ///< Decoder specific information (Audio, video, subtitle context)
    dec_input_cb dec_input;             ///< Decoder packet input function callback
    dec_decode_cb dec_decode;           ///< Decoder decoding function callback
    dec_flush_cb dec_flush;             ///< Decoder buffer flusher function callback
    dec_abort_cb dec_abort;             ///< Decoder abort callback; unblocks buffer waits before thread shutdown
    dec_close_cb dec_close;             ///< Decoder close function callback
    dec_get_buffers_cb dec_get_buffers; ///< Decoder buffer status getter callback
};

/**
 * @brief Creates a decoder for a single stream, opening the codec and negotiating hardware decode if enabled.
 *
 * Allocates and opens an AVCodecContext for the stream's codec. If hardware decoding is enabled library-wide
 * and @p hw_device_types allows it, attempts to find and validate a matching hardware device before falling
 * back to software decoding.
 *
 * @param stream Stream to decode; must not be NULL.
 * @param sync_timer Playback sync timer; the decoder takes ownership and closes it in Kit_CloseDecoder().
 * @param thread_count Requested libavcodec thread count (0 lets ffmpeg pick); disabled if codec has no
 *     frame/slice threading support.
 *
 * @param hw_device_types Bitmask of Kit_HardwareDeviceType values allowed for hardware decode.
 * @param dec_input Packet input callback.
 * @param dec_decode Frame decode callback.
 * @param dec_flush Buffer flush callback.
 * @param dec_abort Buffer wait abort callback.
 * @param dec_close Resource close callback.
 * @param dec_get_buffers Buffer state getter callback.
 * @param userdata Decoder-type-specific context, stored as-is and passed back to all callbacks.
 * @return New decoder, or NULL on allocation/codec-open failure (Kit_SetError() is called).
 */
KIT_LOCAL Kit_Decoder *Kit_CreateDecoder(
    AVStream *stream,
    Kit_Timer *sync_timer,
    int thread_count,
    unsigned int hw_device_types,
    dec_input_cb dec_input,
    dec_decode_cb dec_decode,
    dec_flush_cb dec_flush,
    dec_abort_cb dec_abort,
    dec_close_cb dec_close,
    dec_get_buffers_cb dec_get_buffers,
    void *userdata
);

/**
 * @brief Closes and frees a decoder: runs dec_close, frees the codec context and sync timer, then the struct.
 *
 * @param dec Pointer to the decoder pointer; set to NULL after closing. No-op if NULL or already-NULL.
 */
KIT_LOCAL void Kit_CloseDecoder(Kit_Decoder **dec);

/**
 * @brief Gets the source stream index this decoder was created for.
 *
 * @param decoder Decoder to query.
 * @return Stream index, or -1 if @p decoder is NULL.
 */
KIT_LOCAL int Kit_GetDecoderStreamIndex(const Kit_Decoder *decoder);

/**
 * @brief Retrieves codec name/description/thread-count information for the decoder.
 *
 * @param decoder Decoder to query; if NULL, @p codec is zeroed instead.
 * @param codec Output codec info struct; must not be NULL.
 * @return 0 on success, 1 if @p decoder was NULL (in which case @p codec is zeroed, not left untouched).
 */
KIT_LOCAL int Kit_GetDecoderCodecInfo(const Kit_Decoder *decoder, Kit_Codec *codec);

/**
 * @brief Runs the decoder's dec_decode callback to pull one decoded frame out of the codec.
 *
 * @param decoder Decoder to run; must not be NULL.
 * @param pts Receives the decoded frame's presentation timestamp on success.
 * @return true if a frame was decoded, false if none was available.
 */
KIT_LOCAL bool Kit_RunDecoder(const Kit_Decoder *decoder, double *pts);

/**
 * @brief Feeds one packet into the decoder via its dec_input callback.
 *
 * @param decoder Decoder to feed; must not be NULL.
 * @param packet Packet to submit, or NULL to signal draining/EOF depending on the callback's contract.
 * @return KIT_DEC_INPUT_OK, KIT_DEC_INPUT_RETRY (internal queue full, retry later), or KIT_DEC_INPUT_EOF.
 */
KIT_LOCAL Kit_DecoderInputResult Kit_AddDecoderPacket(const Kit_Decoder *decoder, const AVPacket *packet);

/**
 * @brief Flushes buffered output via dec_flush and resets the underlying libavcodec buffers.
 *
 * @param decoder Decoder to flush; no-op if NULL.
 */
KIT_LOCAL void Kit_ClearDecoderBuffers(Kit_Decoder *decoder);

/**
 * @brief Unblocks any buffer waits (input/output) via dec_abort, so a decoder thread can shut down promptly.
 *
 * This does not stop the decoder thread by itself; it only wakes it up if it is blocked on a full/empty
 * buffer. Callers must still signal the thread to stop separately.
 *
 * @param decoder Decoder to abort; no-op if NULL.
 */
KIT_LOCAL void Kit_AbortDecoder(Kit_Decoder *decoder);

/**
 * @brief Gets the decoder's output buffer fill level and capacity via dec_get_buffers.
 *
 * @param decoder Decoder to query.
 * @param length Receives current buffered item count.
 * @param capacity Receives buffer capacity.
 * @return 0 on success, 1 if @p decoder is NULL or has no dec_get_buffers callback.
 */
KIT_LOCAL int Kit_GetDecoderBufferState(const Kit_Decoder *decoder, unsigned int *length, unsigned int *capacity);

#endif // KITDECODER_H
