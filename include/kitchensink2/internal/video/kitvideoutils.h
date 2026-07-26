#ifndef KITVIDEOUTILS_H
#define KITVIDEOUTILS_H

/**
 * @brief Conversion helpers between SDL and FFmpeg pixel formats and hardware device types.
 *
 * @file kitvideoutils.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/kitformat.h"

/**
 * @brief Picks the best supported FFmpeg pixel format for converting from the given source format.
 *
 * Chooses the closest match (by FFmpeg's own loss heuristic) out of a fixed list of formats that
 * this library and SDL both know how to handle (YUV420P, YUYV422, UYVY422, NV12, NV21, RGB/BGR24,
 * RGB/BGR555, RGB/BGR565, BGRA, RGBA).
 *
 * @param fmt Source pixel format to find a supported match for
 * @return Best matching supported AVPixelFormat
 */
enum AVPixelFormat Kit_FindBestAVPixelFormat(enum AVPixelFormat fmt);

/**
 * @brief Maps an FFmpeg pixel format to the matching SDL pixel format.
 *
 * Any format not explicitly handled (i.e. not one of the planar/packed YUV formats listed) falls
 * back to SDL_PIXELFORMAT_RGBA32.
 *
 * @param fmt FFmpeg pixel format to convert
 * @return Matching SDL pixel format constant
 */
unsigned int Kit_FindSDLPixelFormat(enum AVPixelFormat fmt);

/**
 * @brief Maps an SDL pixel format to the matching FFmpeg pixel format.
 *
 * @param fmt SDL pixel format constant to convert
 * @return Matching AVPixelFormat, or AV_PIX_FMT_NONE if the SDL format is not supported
 */
enum AVPixelFormat Kit_FindAVPixelFormat(unsigned int fmt);

/**
 * @brief Maps an FFmpeg hardware device type to the matching Kit hardware device type.
 *
 * @param type FFmpeg hardware device type to convert
 * @return Matching Kit_HardwareDeviceType, or KIT_HWDEVICE_TYPE_NONE if unrecognized
 */
Kit_HardwareDeviceType Kit_FindHWDeviceType(enum AVHWDeviceType type);

#endif // KITVIDEOUTILS_H
