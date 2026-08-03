#ifndef KITSUBTITLE_H
#define KITSUBTITLE_H

/**
 * @brief Subtitle decoder: wraps a Kit_Decoder for a subtitle stream, dispatching decoded AVSubtitle
 * frames to a codec-specific Kit_SubtitleRenderer (libass text renderer or bitmap image renderer)
 * and exposing the resulting packed texture atlas / raw frame data for playback.
 *
 * @file kitsubtitle.h
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
 * @brief Creates a subtitle decoder and stream for the given source and stream index.
 *
 * Picks a Kit_SubtitleRenderer based on the stream's codec: text-based codecs (SRT/SSA/ASS/...)
 * use the libass renderer and require the library to have been initialized with KIT_INIT_ASS,
 * otherwise creation fails; bitmap codecs (DVD/DVB/PGS) use the image renderer. Output pixel
 * format is always SDL_PIXELFORMAT_RGBA32. On failure, this function also closes @p sync_timer.
 *
 * @param src source the subtitle stream belongs to
 * @param config subtitle stream configuration; the buffer size and font hinting mode are
 * copied from it (the pointer is not retained)
 * @param thread_count FFmpeg codec thread count, 0 for autodetect
 * @param sync_timer sync timer for the decoder; ownership is transferred to the created decoder,
 * or closed by this function if creation fails
 *
 * @param stream_index index of the subtitle stream within the source's format context
 * @param video_w video frame width, used for subtitle coordinate scaling
 * @param video_h video frame height, used for subtitle coordinate scaling
 * @param screen_w target output width for rendering/scaling subtitles
 * @param screen_h target output height for rendering/scaling subtitles
 * @return newly created decoder, or NULL on failure
 */
KIT_LOCAL Kit_Decoder *Kit_CreateSubtitleDecoder(
    const Kit_Source *src,
    const Kit_PlayerSubtitleConfig *config,
    int thread_count,
    Kit_Timer *sync_timer,
    int stream_index,
    int video_w,
    int video_h,
    int screen_w,
    int screen_h
);

/**
 * @brief Renders the decoder's currently active subtitle items into @p texture via its atlas.
 *
 * @param dec subtitle decoder
 * @param texture destination texture that backs the atlas's packed regions
 * @param sync_ts current playback timestamp used to select which subtitles are visible
 */
KIT_LOCAL void Kit_GetSubtitleDecoderSDLTexture(const Kit_Decoder *dec, SDL_Texture *texture, double sync_ts);

/**
 * @brief Updates the output screen size used for subtitle positioning/scaling.
 *
 * @param dec subtitle decoder
 * @param w new output width
 * @param h new output height
 */
KIT_LOCAL void Kit_SetSubtitleDecoderSize(const Kit_Decoder *dec, int w, int h);

/**
 * @brief Copies up to @p limit source/target rects of the decoder's currently packed atlas items.
 *
 * @param dec subtitle decoder
 * @param sources destination array for source rects, or NULL to skip
 * @param targets destination array for target rects, or NULL to skip
 * @param limit maximum number of items to copy
 * @return number of items actually copied
 */
KIT_LOCAL int
Kit_GetSubtitleDecoderSDLTextureInfo(const Kit_Decoder *dec, SDL_Rect *sources, SDL_Rect *targets, int limit);

/**
 * @brief Retrieves the subtitle output pixel format.
 *
 * @param decoder subtitle decoder, or NULL to get a zeroed-out format
 * @param output destination struct to fill
 * @return 0 on success, 1 if @p decoder was NULL (output is zeroed instead)
 */
KIT_LOCAL int Kit_GetSubtitleDecoderOutputFormat(const Kit_Decoder *decoder, Kit_SubtitleOutputFormat *output);

/**
 * @brief Retrieves the decoder's currently active subtitles as raw RGBA pixel buffers rather
 * than a packed texture atlas.
 *
 * @param dec subtitle decoder
 * @param items receives a pointer to the renderer-owned array of raw pixel buffer pointers
 * @param sources receives a pointer to the renderer-owned array of source rects
 * @param targets receives a pointer to the renderer-owned array of target rects
 * @param sync_ts current playback timestamp used to select which subtitles are visible
 * @return number of frames available
 */
KIT_LOCAL int Kit_GetSubtitleDecoderRawFrames(
    const Kit_Decoder *dec, unsigned char ***items, SDL_Rect **sources, SDL_Rect **targets, double sync_ts
);

#endif // KITSUBTITLE_H
