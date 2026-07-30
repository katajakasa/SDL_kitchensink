#ifndef KITSUBIMAGE_H
#define KITSUBIMAGE_H

/**
 * @brief Bitmap subtitle renderer for image-based subtitle codecs (DVD/DVB/PGS). Converts paletted
 * AVSubtitle rects to RGBA surfaces and passes them through a Kit_PacketBuffer of
 * Kit_SubtitlePacket slots. That buffer is created with a NULL ref callback, so it can only be
 * consumed with Kit_ReadPacketBuffer() (move semantics); Kit_BeginPacketBufferRead() must not be
 * used on it.
 *
 * @file kitsubimage.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/internal/kitdecoder.h"
#include "kitchensink2/internal/subtitle/renderers/kitsubrenderer.h"
#include "kitchensink2/kitconfig.h"

/**
 * @brief Creates a subtitle renderer that decodes bitmap subtitles (DVD/DVB/PGS).
 *
 * Converts each decoded subtitle's paletted bitmap rects to RGBA32 surfaces and queues them as
 * Kit_SubtitlePacket entries in an internal packet buffer, scaled from video to screen size.
 *
 * @param dec subtitle decoder that owns this renderer's codec context
 * @param video_w video frame width, used to compute the x/y scale factor for subtitle positions
 * @param video_h video frame height, used to compute the x/y scale factor for subtitle positions
 * @param screen_w target output width subtitle coordinates are scaled to
 * @param screen_h target output height subtitle coordinates are scaled to
 * @return newly created renderer, or NULL on failure
 */
KIT_LOCAL Kit_SubtitleRenderer *
Kit_CreateImageSubtitleRenderer(Kit_Decoder *dec, int video_w, int video_h, int screen_w, int screen_h);

#endif // KITSUBIMAGE_H
