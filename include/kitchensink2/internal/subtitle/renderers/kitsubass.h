#ifndef KITSUBASS_H
#define KITSUBASS_H

/**
 * @brief libass-backed subtitle renderer for text-based subtitle codecs (SRT/SSA/ASS/...). Renders
 * AVSubtitle "ass" chunks through libass and converts the resulting images to RGBA bitmaps.
 *
 * @file kitsubass.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/internal/kitdecoder.h"
#include "kitchensink2/internal/subtitle/renderers/kitsubrenderer.h"
#include "kitchensink2/kitconfig.h"

/**
 * @brief Creates a subtitle renderer that decodes text subtitles (SRT/SSA/ASS/...) via libass.
 *
 * Requires the library to have been initialized with KIT_INIT_ASS (i.e. libass is loaded);
 * fails with an error if it was not. Font attachment streams found in @p format_ctx are handed
 * to libass, and the decoder's subtitle_header (if any) is used to initialize the libass track.
 *
 * @param format_ctx format context of the source, used to find embedded font attachments
 * @param dec subtitle decoder that owns this renderer's codec context and subtitle header
 * @param video_w video frame width, used as libass storage size for coordinate scaling
 * @param video_h video frame height, used as libass storage size for coordinate scaling
 * @param screen_w initial libass frame (render target) width
 * @param screen_h initial libass frame (render target) height
 * @return newly created renderer, or NULL on failure
 */
KIT_LOCAL Kit_SubtitleRenderer *Kit_CreateASSSubtitleRenderer(
    const AVFormatContext *format_ctx, Kit_Decoder *dec, int video_w, int video_h, int screen_w, int screen_h
);

#endif // KITSUBASS_H
