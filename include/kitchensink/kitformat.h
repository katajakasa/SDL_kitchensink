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

typedef enum
{
    KIT_HWDEVICE_TYPE_NONE = 0,
    KIT_HWDEVICE_TYPE_UNKNOWN,
    KIT_HWDEVICE_TYPE_VDPAU,
    KIT_HWDEVICE_TYPE_CUDA,
    KIT_HWDEVICE_TYPE_VAAPI,
    KIT_HWDEVICE_TYPE_DXVA2,
    KIT_HWDEVICE_TYPE_QSV,
    KIT_HWDEVICE_TYPE_VIDEOTOOLBOX,
    KIT_HWDEVICE_TYPE_D3D11VA,
    KIT_HWDEVICE_TYPE_DRM,
    KIT_HWDEVICE_TYPE_OPENCL,
    KIT_HWDEVICE_TYPE_MEDIACODEC,
    KIT_HWDEVICE_TYPE_VULKAN,
} Kit_HardwareDeviceType;

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
