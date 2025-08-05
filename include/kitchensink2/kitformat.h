#ifndef KITFORMAT_H
#define KITFORMAT_H

/**
 * @brief Audio/video output format type
 *
 * @file kitformat.h
 * @author Tuomas Virtanen
 * @date 2018-06-25
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "kitchensink/kitconfig.h"

#define KIT_MAX_HW_DEVICES 32

typedef enum
{
    KIT_HWDEVICE_TYPE_NONE = 0,
    KIT_HWDEVICE_TYPE_VDPAU = 0x1,
    KIT_HWDEVICE_TYPE_CUDA = 0x2,
    KIT_HWDEVICE_TYPE_VAAPI = 0x4,
    KIT_HWDEVICE_TYPE_DXVA2 = 0x8,
    KIT_HWDEVICE_TYPE_QSV = 0x10,
    KIT_HWDEVICE_TYPE_VIDEOTOOLBOX = 0x20,
    KIT_HWDEVICE_TYPE_D3D11VA = 0x40,
    KIT_HWDEVICE_TYPE_DRM = 0x80,
    KIT_HWDEVICE_TYPE_OPENCL = 0x100,
    KIT_HWDEVICE_TYPE_MEDIACODEC = 0x200,
    KIT_HWDEVICE_TYPE_VULKAN = 0x400,
    KIT_HWDEVICE_TYPE_ALL = 0xFFFFFFFF,
} Kit_HardwareDeviceType;

/**
 * @brief Used to request specific type for formats for output video
 *
 * Note that any requests here will cause software conversion, which may be slow!
 */
typedef struct Kit_VideoFormatRequest {
    unsigned int
        hw_device_types; ///< Bitmap of allowed hardware acceleration types. Defaults to KIT_HWDEVICE_TYPE_ALL.
    unsigned int format; ///< Requested surface format. Defaults to SDL_PIXELFORMAT_UNKNOWN (allow any).
    int width;           ///< Requested width in pixels. Defaults to -1 (no change).
    int height;          ///< Requested height in pixels. Defaults to -1 (no change).
} Kit_VideoFormatRequest;

/**
 * @brief Sets default values for Kit_VideoFormatRequest
 *
 * @param request Request to reset to default values
 */
KIT_API void Kit_ResetVideoFormatRequest(Kit_VideoFormatRequest *request);

/**
 * @brief Used to request specific type for formats for output audio
 *
 * Note that any requests here will cause software conversion, which may be slow!
 */
typedef struct Kit_AudioFormatRequest {
    unsigned int format; ///< Requested sample format. Defaults to 0 (no change).
    int is_signed;       ///< Signedness, 1 = signed, 0 = unsigned. Defaults to -1 (no change).
    int bytes;           ///< Bytes per sample per channel. Defaults to -1 (no change).
    int sample_rate;     ///< Sampling rate. Defaults to -1 (no change).
    int channels;        ///< Channels. Defaults to -1 (no change).
} Kit_AudioFormatRequest;

/**
 * @brief Sets default values for Kit_AudioFormatRequest
 *
 * @param request Request to reset to default values
 */
KIT_API void Kit_ResetAudioFormatRequest(Kit_AudioFormatRequest *request);

/**
 * @brief Contains information about the subtitle data format coming out from the player
 */
typedef struct Kit_SubtitleOutputFormat {
    unsigned int format; ///< SDL_PixelFormat for surface format description
} Kit_SubtitleOutputFormat;

/**
 * @brief Contains information about the video data format coming out from the player
 */
typedef struct Kit_VideoOutputFormat {
    unsigned int hw_device_type; ///< Kit_HardwareDeviceType, if enabled.
    unsigned int format;         ///< SDL_PixelFormat for surface format description
    int width;                   ///< Width in pixels
    int height;                  ///< Height in pixels
} Kit_VideoOutputFormat;

/**
 * @brief Contains information about the audio data format coming out from the player
 */
typedef struct Kit_AudioOutputFormat {
    unsigned int format; ///< SDL_AudioFormat for format description
    int is_signed;       ///< Signedness, 1 = signed, 0 = unsigned
    int bytes;           ///< Bytes per sample per channel
    int sample_rate;     ///< Sampling rate
    int channels;        ///< Channels
} Kit_AudioOutputFormat;

#ifdef __cplusplus
}
#endif

#endif // KITFORMAT_H
