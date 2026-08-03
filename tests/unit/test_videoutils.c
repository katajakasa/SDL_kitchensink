/**
 * Unit tests for kitvideoutils.h, the pure lookup-table conversions between
 * SDL pixel formats/hw device types and their FFmpeg (AVPixelFormat /
 * AVHWDeviceType) counterparts. No I/O or SDL/libav init is required.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL_pixels.h>
#include <libavutil/hwcontext.h>

#include "kitchensink2/internal/video/kitvideoutils.h"

/**
 * @brief Known AV pixel formats map to the matching SDL format; anything unsupported falls back to RGBA32.
 */
static void test_find_sdl_pixel_format(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_int_equal(Kit_FindSDLPixelFormat(AV_PIX_FMT_YUV420P), SDL_PIXELFORMAT_YV12);
    assert_int_equal(Kit_FindSDLPixelFormat(AV_PIX_FMT_YUYV422), SDL_PIXELFORMAT_YUY2);
    assert_int_equal(Kit_FindSDLPixelFormat(AV_PIX_FMT_NV12), SDL_PIXELFORMAT_NV12);
    assert_int_equal(Kit_FindSDLPixelFormat(AV_PIX_FMT_NV21), SDL_PIXELFORMAT_NV21);
    // Anything unsupported falls back to RGBA32
    assert_int_equal(Kit_FindSDLPixelFormat(AV_PIX_FMT_GBRP), SDL_PIXELFORMAT_RGBA32);
}

/**
 * @brief Known SDL pixel formats map to the matching AV format; an unsupported SDL format maps to AV_PIX_FMT_NONE.
 */
static void test_find_av_pixel_format(void **state) {
    (void)state;
    // Arrange / Act / Assert: direct mappings, plus the unsupported-format NONE case
    assert_int_equal(Kit_FindAVPixelFormat(SDL_PIXELFORMAT_YV12), AV_PIX_FMT_YUV420P);
    assert_int_equal(Kit_FindAVPixelFormat(SDL_PIXELFORMAT_NV12), AV_PIX_FMT_NV12);
    assert_int_equal(Kit_FindAVPixelFormat(SDL_PIXELFORMAT_RGBA32), AV_PIX_FMT_RGBA);
    assert_int_equal(Kit_FindAVPixelFormat(SDL_PIXELFORMAT_BGRA32), AV_PIX_FMT_BGRA);
    assert_int_equal(Kit_FindAVPixelFormat(SDL_PIXELFORMAT_ABGR32), AV_PIX_FMT_ABGR);
    assert_int_equal(Kit_FindAVPixelFormat(SDL_PIXELFORMAT_IYUV), AV_PIX_FMT_YUV420P);
    assert_int_equal(Kit_FindAVPixelFormat(SDL_PIXELFORMAT_YVYU), AV_PIX_FMT_YVYU422);
    assert_int_equal(Kit_FindAVPixelFormat(SDL_PIXELFORMAT_RGB888), AV_PIX_FMT_0RGB32);
    assert_int_equal(Kit_FindAVPixelFormat(SDL_PIXELFORMAT_BGR888), AV_PIX_FMT_0BGR32);
    assert_int_equal(Kit_FindAVPixelFormat(SDL_PIXELFORMAT_INDEX1LSB), AV_PIX_FMT_NONE);
}

/**
 * @brief Every directly-supported AV format round-trips through Kit_FindSDLPixelFormat -> Kit_FindAVPixelFormat
 * unchanged.
 */
static void test_pixel_format_round_trip(void **state) {
    (void)state;
    // Arrange
    const enum AVPixelFormat formats[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_UYVY422,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_NV21,
    };

    // Act / Assert: each format round-trips unchanged
    for(size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        assert_int_equal(Kit_FindAVPixelFormat(Kit_FindSDLPixelFormat(formats[i])), formats[i]);
    }
}

/**
 * @brief Kit_FindBestAVPixelFormat() returns already-supported input unchanged, and degrades unsupported formats to a
 * valid fallback.
 */
static void test_find_best_av_pixel_format(void **state) {
    (void)state;
    // Act / Assert
    // A format already on the supported list maps to itself.
    assert_int_equal(Kit_FindBestAVPixelFormat(AV_PIX_FMT_YUV420P), AV_PIX_FMT_YUV420P);

    // Act / Assert
    // An unsupported format maps to a valid fallback format.
    const enum AVPixelFormat best = Kit_FindBestAVPixelFormat(AV_PIX_FMT_YUV444P);
    assert_int_not_equal(best, AV_PIX_FMT_NONE);
}

/**
 * @brief AVHWDeviceType values classify into the matching Kit_HWDeviceType, including NONE mapping to NONE.
 */
static void test_find_hw_device_type(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_int_equal(Kit_FindHWDeviceType(AV_HWDEVICE_TYPE_NONE), KIT_HWDEVICE_TYPE_NONE);
    assert_int_equal(Kit_FindHWDeviceType(AV_HWDEVICE_TYPE_VAAPI), KIT_HWDEVICE_TYPE_VAAPI);
    assert_int_equal(Kit_FindHWDeviceType(AV_HWDEVICE_TYPE_CUDA), KIT_HWDEVICE_TYPE_CUDA);
    assert_int_equal(Kit_FindHWDeviceType(AV_HWDEVICE_TYPE_VULKAN), KIT_HWDEVICE_TYPE_VULKAN);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_find_sdl_pixel_format),
        cmocka_unit_test(test_find_av_pixel_format),
        cmocka_unit_test(test_pixel_format_round_trip),
        cmocka_unit_test(test_find_best_av_pixel_format),
        cmocka_unit_test(test_find_hw_device_type),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
