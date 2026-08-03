/**
 * Tests for Kit_ResetVideoFormatRequest/Kit_ResetAudioFormatRequest
 * (kitchensink.h), which reset a caller-provided format request struct back
 * to "auto-detect everything" sentinel values before Kit_CreatePlayer() uses
 * it to negotiate output format.
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

#include "kitchensink3/kitchensink.h"

/**
 * @brief Kit_ResetVideoFormatRequest() must restore every field to its "auto" sentinel.
 */
static void test_reset_video_format_request(void **state) {
    (void)state;
    // Arrange
    Kit_VideoFormatRequest request = {1, 2, 3, 4};

    // Act
    Kit_ResetVideoFormatRequest(&request);

    // Assert
    assert_int_equal(request.hw_device_types, KIT_HWDEVICE_TYPE_ALL);
    assert_int_equal(request.format, SDL_PIXELFORMAT_UNKNOWN);
    assert_int_equal(request.width, -1);
    assert_int_equal(request.height, -1);
}

/**
 * @brief Kit_ResetAudioFormatRequest() must restore every field to its "auto" sentinel.
 */
static void test_reset_audio_format_request(void **state) {
    (void)state;
    // Arrange
    Kit_AudioFormatRequest request = {1, 2, 3, 4, KIT_LAYOUT_STEREO};

    // Act
    Kit_ResetAudioFormatRequest(&request);

    // Assert
    assert_int_equal(request.format, 0);
    assert_int_equal(request.is_signed, -1);
    assert_int_equal(request.bytes, -1);
    assert_int_equal(request.sample_rate, -1);
    assert_int_equal(request.layout, KIT_LAYOUT_UNKNOWN);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_reset_video_format_request),
        cmocka_unit_test(test_reset_audio_format_request),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
