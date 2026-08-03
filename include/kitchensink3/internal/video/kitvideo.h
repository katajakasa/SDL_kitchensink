#ifndef KITVIDEO_H
#define KITVIDEO_H

/**
 * @brief Internal video decoder: decodes, converts/scales and synchronizes a video stream.
 *
 * @file kitvideo.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <SDL3/SDL_render.h>

#include "kitchensink3/internal/kitdecoder.h"
#include "kitchensink3/internal/kittimer.h"
#include "kitchensink3/kitconfig.h"
#include "kitchensink3/kitformat.h"
#include "kitchensink3/kitplayer.h"
#include "kitchensink3/kitsource.h"

/**
 * @brief Creates and initializes a video decoder for the given stream.
 *
 * Sets up the (lazily-created) sws conversion context and packet buffer for the selected stream,
 * picking an output pixel format from format_request if set, otherwise from the hardware type or
 * codec pixel format. On failure, the decoder is destroyed and NULL is returned; in all cases
 * ownership of sync_timer is taken by this call, even on failure.
 *
 * @param src Source to read the stream from
 * @param format_request Requested output video format, or defaults where fields are unset
 * @param config Video stream configuration; the buffer size and sync thresholds are copied
 *        from it (the pointer is not retained)
 * @param thread_count FFmpeg codec thread count, 0 for autodetect
 * @param sync_timer Sync timer to attach to the decoder (ownership transferred to this call)
 * @param stream_index Index of the video stream to decode
 * @return New decoder on success, NULL on error (see Kit_GetError())
 */
KIT_LOCAL Kit_Decoder *Kit_CreateVideoDecoder(
    const Kit_Source *src,
    const Kit_VideoFormatRequest *format_request,
    const Kit_PlayerVideoConfig *config,
    int thread_count,
    Kit_Timer *sync_timer,
    int stream_index
);

/**
 * @brief Reads the next synchronized video frame and uploads it to an SDL texture.
 *
 * Runs on the caller's thread. Internally reads and syncs a frame against the decoder's sync
 * timer/serial (skipping stale or too-early/too-late frames), then updates the texture using
 * SDL_UpdateYUVTexture, SDL_UpdateNVTexture or SDL_UpdateTexture depending on the output pixel
 * format. Does nothing (returns 1) if no frame is currently ready to be displayed.
 *
 * @param dec Video decoder instance
 * @param texture Previously allocated SDL texture matching the decoder's output format
 * @param area Optional pointer to receive the rendered frame's area, or NULL
 * @return 0 if the texture was updated, 1 if no frame was available
 */
KIT_LOCAL int Kit_GetVideoDecoderSDLTexture(Kit_Decoder *dec, SDL_Texture *texture, SDL_Rect *area);

/**
 * @brief Locks the current synchronized video frame for direct (raw) pixel access.
 *
 * Runs on the caller's thread and is gated by the same sync timer/serial checks as
 * Kit_GetVideoDecoderSDLTexture(). On success, data/line_size point directly at the decoder's
 * internal frame and Kit_UnlockVideoDecoderRaw() must be called to release it; on failure (return
 * value 1), no lock is held and Kit_UnlockVideoDecoderRaw() must NOT be called.
 *
 * @param decoder Video decoder instance
 * @param data Optional pointer to receive the frame's data plane pointers, or NULL
 * @param line_size Optional pointer to receive the frame's line size array, or NULL
 * @param area Optional pointer to receive the frame's area, or NULL
 * @return 0 on success (frame locked), 1 if no frame was available
 */
KIT_LOCAL int Kit_LockVideoDecoderRaw(Kit_Decoder *decoder, unsigned char ***data, int **line_size, SDL_Rect *area);

/**
 * @brief Releases a frame previously locked with Kit_LockVideoDecoderRaw().
 *
 * @param decoder Video decoder instance
 */
KIT_LOCAL void Kit_UnlockVideoDecoderRaw(Kit_Decoder *decoder);

/**
 * @brief Retrieves the negotiated output video format for a decoder.
 *
 * If dec is NULL, output is zeroed and 1 is returned instead of failing.
 *
 * @param dec Video decoder instance, or NULL
 * @param output Output format struct to fill in
 * @return 0 on success, 1 if dec was NULL
 */
KIT_LOCAL int Kit_GetVideoDecoderOutputFormat(const Kit_Decoder *dec, Kit_VideoOutputFormat *output);

#endif // KITVIDEO_H
