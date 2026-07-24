#ifndef KITUTILS_H
#define KITUTILS_H

/**
 * @brief Helpful utilities
 *
 * @file kitutils.h
 * @author Tuomas Virtanen
 * @date 2018-06-25
 */

#include "kitchensink2/kitconfig.h"
#include "kitchensink2/kitformat.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns a descriptive string for SDL audio format types
 *
 * @param type SDL_AudioFormat
 * @return Format string, eg. "AUDIO_S8".
 */
KIT_API const char *Kit_GetSDLAudioFormatString(unsigned int type);

/**
 * @brief Returns a descriptive string for SDL pixel format types
 *
 * @param type SDL_PixelFormat
 * @return Format string, eg. "SDL_PIXELFORMAT_YV12"
 */
KIT_API const char *Kit_GetSDLPixelFormatString(unsigned int type);

/**
 * @brief Returns a descriptive string for Kitchensink stream types
 *
 * @param type Kit_StreamType
 * @return Format string, eg. "KIT_STREAMTYPE_VIDEO"
 */
KIT_API const char *Kit_GetKitStreamTypeString(unsigned int type);

/**
 * @brief Returns a descriptive string for Kitchensink hardware video decoder type
 * @param type Kit_HardwareDeviceType
 * @return Format string, eg. "KIT_HWDEVICE_TYPE_VDPAU"
 */
KIT_API const char *Kit_GetHardwareDecoderTypeString(unsigned int type);

/**
 * @brief Returns the SDL2 channel count for a kitchensink audio channel layout
 *
 * @param layout Audio channel layout
 * @return Channel count (e.g. 6 for KIT_LAYOUT_5POINT1), or -1 for
 *         KIT_LAYOUT_UNKNOWN or invalid values.
 */
KIT_API int Kit_GetChannelLayoutCount(Kit_AudioChannelLayout layout);

/**
 * @brief Returns the kitchensink audio channel layout for an SDL2 channel count
 *
 * @param channels SDL2 channel count (1, 2, 3, 4, 6, 7 or 8)
 * @return Matching layout, or KIT_LAYOUT_UNKNOWN for any other count.
 */
KIT_API Kit_AudioChannelLayout Kit_GetChannelLayoutFromCount(int channels);

/**
 * @brief Returns a descriptive string for a kitchensink audio channel layout
 *
 * @param layout Audio channel layout
 * @return Format string, eg. "KIT_LAYOUT_5POINT1". Invalid values return
 *         "KIT_LAYOUT_UNKNOWN", never NULL.
 */
KIT_API const char *Kit_GetChannelLayoutString(Kit_AudioChannelLayout layout);

#ifdef __cplusplus
}
#endif

#endif // KITUTILS_H
