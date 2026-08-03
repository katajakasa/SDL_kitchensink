#ifndef KITHELPERS_H
#define KITHELPERS_H

/**
 * @brief Small internal utility functions shared across decoders/demuxer (timing, stream, and math helpers).
 *
 * @file kithelpers.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/kitconfig.h"
#include <libavformat/avformat.h>
#include <stdbool.h>

/**
 * @brief Returns the current system time in seconds, using FFmpeg's monotonic clock source.
 *
 * @return Current time in seconds, as a fractional value derived from av_gettime_relative()
 */
KIT_LOCAL double Kit_GetSystemTime(void);

/**
 * @brief Checks whether an attachment stream is a font attachment usable for subtitle rendering.
 *
 * Returns false for any stream that is not an AVMEDIA_TYPE_ATTACHMENT stream. Otherwise checks the
 * stream's "mimetype" metadata tag against a fixed list of known TrueType/OpenType MIME types.
 *
 * @param stream Stream to check
 * @return true if the stream is a recognized font attachment, false otherwise
 */
KIT_LOCAL bool Kit_StreamIsFontAttachment(const AVStream *stream);

/**
 * @brief Returns the larger of two integers.
 *
 * @param a First value
 * @param b Second value
 * @return The larger of a and b
 */
KIT_LOCAL int Kit_max(int a, int b);

/**
 * @brief Returns the smaller of two integers.
 *
 * @param a First value
 * @param b Second value
 * @return The smaller of a and b
 */
KIT_LOCAL int Kit_min(int a, int b);

/**
 * @brief Clamps an integer value to the inclusive [min, max] range.
 *
 * @param v Value to clamp
 * @param min Lower bound
 * @param max Upper bound
 * @return v clamped to [min, max]
 */
KIT_LOCAL int Kit_clamp(int v, int min, int max);

#endif // KITHELPERS_H
