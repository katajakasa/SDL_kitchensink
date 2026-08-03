/**
 * Tests for the public stringification and channel-layout helper API in
 * kitchensink.h (Kit_Get*String, Kit_GetChannelLayoutCount/FromCount). Pure
 * lookups, no Kit_Init() required.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL_audio.h>
#include <SDL_pixels.h>

#include "kitchensink2/kitchensink.h"

/**
 * @brief Known stream types map to their enum-name strings; unknown values return NULL.
 */
static void test_stream_type_strings(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_string_equal(Kit_GetKitStreamTypeString(KIT_STREAMTYPE_VIDEO), "KIT_STREAMTYPE_VIDEO");
    assert_string_equal(Kit_GetKitStreamTypeString(KIT_STREAMTYPE_AUDIO), "KIT_STREAMTYPE_AUDIO");
    assert_null(Kit_GetKitStreamTypeString(0xFFFF)); // unknown values return NULL
}

/**
 * @brief The SDL audio/pixel format and hardware decoder name getters return non-NULL for valid inputs.
 */
static void test_format_strings(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_non_null(Kit_GetSDLAudioFormatString(AUDIO_S16));
    assert_non_null(Kit_GetSDLPixelFormatString(SDL_PIXELFORMAT_YV12));
    assert_non_null(Kit_GetHardwareDecoderTypeString(KIT_HWDEVICE_TYPE_VAAPI));
}

/**
 * @brief Each supported channel layout reports its correct speaker count; unknown reports -1.
 */
static void test_channel_layout_count(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_int_equal(Kit_GetChannelLayoutCount(KIT_LAYOUT_MONO), 1);
    assert_int_equal(Kit_GetChannelLayoutCount(KIT_LAYOUT_STEREO), 2);
    assert_int_equal(Kit_GetChannelLayoutCount(KIT_LAYOUT_5POINT1), 6);
    assert_int_equal(Kit_GetChannelLayoutCount(KIT_LAYOUT_7POINT1), 8);
    assert_int_equal(Kit_GetChannelLayoutCount(KIT_LAYOUT_UNKNOWN), -1);
}

/**
 * @brief An unambiguous channel count maps back to the expected layout; undefined counts return UNKNOWN.
 */
static void test_channel_layout_from_count(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_int_equal(Kit_GetChannelLayoutFromCount(1), KIT_LAYOUT_MONO);
    assert_int_equal(Kit_GetChannelLayoutFromCount(2), KIT_LAYOUT_STEREO);
    assert_int_equal(Kit_GetChannelLayoutFromCount(6), KIT_LAYOUT_5POINT1);
    assert_int_equal(Kit_GetChannelLayoutFromCount(5), KIT_LAYOUT_UNKNOWN);
    assert_int_equal(Kit_GetChannelLayoutFromCount(0), KIT_LAYOUT_UNKNOWN);
}

/**
 * @brief Every supported layout round-trips through count -> from_count unchanged.
 * Guards against the two lookup tables drifting out of sync when a layout is added.
 */
static void test_channel_layout_round_trip(void **state) {
    (void)state;
    // Arrange
    const Kit_AudioChannelLayout layouts[] = {
        KIT_LAYOUT_MONO,
        KIT_LAYOUT_STEREO,
        KIT_LAYOUT_2POINT1,
        KIT_LAYOUT_QUAD,
        KIT_LAYOUT_5POINT1,
        KIT_LAYOUT_6POINT1,
        KIT_LAYOUT_7POINT1,
    };

    // Act / Assert: count -> from_count must land back on the same layout.
    for(size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
        const int count = Kit_GetChannelLayoutCount(layouts[i]);
        assert_int_equal(Kit_GetChannelLayoutFromCount(count), layouts[i]);
    }
}

/**
 * @brief A known layout stringifies to its enum name; out-of-range values fall back to "KIT_LAYOUT_UNKNOWN".
 */
static void test_channel_layout_string(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_string_equal(Kit_GetChannelLayoutString(KIT_LAYOUT_5POINT1), "KIT_LAYOUT_5POINT1");
    assert_string_equal(Kit_GetChannelLayoutString((Kit_AudioChannelLayout)999), "KIT_LAYOUT_UNKNOWN");
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_stream_type_strings),
        cmocka_unit_test(test_format_strings),
        cmocka_unit_test(test_channel_layout_count),
        cmocka_unit_test(test_channel_layout_from_count),
        cmocka_unit_test(test_channel_layout_round_trip),
        cmocka_unit_test(test_channel_layout_string),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
