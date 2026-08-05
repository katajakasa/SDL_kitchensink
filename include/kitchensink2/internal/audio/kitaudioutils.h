#ifndef KITAUDIOUTILS_H
#define KITAUDIOUTILS_H

/**
 * @brief Conversion helpers between SDL and FFmpeg audio sample/channel-layout representations.
 *
 * @file kitaudioutils.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/kitconfig.h"
#include "kitchensink2/kitformat.h"

#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

/**
 * @brief Maps an SDL audio format constant to the matching FFmpeg sample format.
 *
 * @param format SDL audio format (e.g. AUDIO_S16SYS)
 * @return Matching AVSampleFormat, or AV_SAMPLE_FMT_NONE if unsupported
 */
KIT_LOCAL enum AVSampleFormat Kit_FindAVSampleFormat(int format);

/**
 * @brief Maps a Kit channel layout to the matching FFmpeg channel layout.
 *
 * Falls back to stereo for KIT_LAYOUT_STEREO and any unrecognized value.
 *
 * @param layout Kit channel layout to convert
 * @param out FFmpeg channel layout to fill in
 */
KIT_LOCAL void Kit_FindAVChannelLayout(Kit_AudioChannelLayout layout, AVChannelLayout *out);

/**
 * @brief Maps an FFmpeg channel layout to the closest matching Kit channel layout, by channel count.
 *
 * @param channel_layout FFmpeg channel layout to inspect
 * @return Closest matching Kit_AudioChannelLayout (falls back to KIT_LAYOUT_STEREO for 2 channels or unmatched
 * counts)
 */
KIT_LOCAL Kit_AudioChannelLayout Kit_FindChannelLayout(const AVChannelLayout *channel_layout);

/**
 * @brief Returns the bytes per sample of the SDL output format an FFmpeg sample format maps to
 * (see Kit_FindSDLSampleFormat).
 *
 * @param fmt FFmpeg sample format
 * @return 1 for 8-bit formats, 4 for 32-bit and 64-bit formats, 2 otherwise (default)
 */
KIT_LOCAL int Kit_FindBytes(enum AVSampleFormat fmt);

/**
 * @brief Maps an FFmpeg sample format to the closest matching SDL audio format constant.
 *
 * The 64-bit formats narrow to their 32-bit SDL equivalent (S64 to AUDIO_S32SYS, DBL to
 * AUDIO_F32SYS), since SDL has no 64-bit audio formats.
 *
 * @param fmt FFmpeg sample format
 * @return Closest matching SDL audio format (defaults to AUDIO_S16SYS for anything without a match)
 */
KIT_LOCAL int Kit_FindSDLSampleFormat(enum AVSampleFormat fmt);

/**
 * @brief Determines whether an FFmpeg sample format is signed.
 *
 * @param fmt FFmpeg sample format
 * @return 0 for unsigned 8-bit formats, 1 for everything else
 */
KIT_LOCAL int Kit_FindSignedness(enum AVSampleFormat fmt);

#endif // KITAUDIOUTILS_H
