/**
 * Unit tests for kitaudioutils.h, the pure lookup-table conversions between
 * SDL audio formats/channel layouts and their FFmpeg (AVSampleFormat /
 * AVChannelLayout) counterparts. No I/O or SDL/libav init is required.
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

#include "kitchensink2/internal/audio/kitaudioutils.h"

/**
 * @brief Known SDL formats map to the matching AV sample format; an unrecognized value falls back to
 * AV_SAMPLE_FMT_NONE.
 */
static void test_find_av_sample_format(void **state) {
    (void)state;
    // Arrange / Act / Assert: known mappings, plus the unrecognized-value fallback
    assert_int_equal(Kit_FindAVSampleFormat(AUDIO_U8), AV_SAMPLE_FMT_U8);
    assert_int_equal(Kit_FindAVSampleFormat(AUDIO_S16SYS), AV_SAMPLE_FMT_S16);
    assert_int_equal(Kit_FindAVSampleFormat(AUDIO_S32SYS), AV_SAMPLE_FMT_S32);
    assert_int_equal(Kit_FindAVSampleFormat(0xBEEF), AV_SAMPLE_FMT_NONE);
}

/**
 * @brief Byte width per sample is correct for planar and packed U8/S16/S32; unlisted formats (FLT)
 * fall back to 2, the S16 width they are converted to.
 */
static void test_find_bytes(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_int_equal(Kit_FindBytes(AV_SAMPLE_FMT_U8), 1);
    assert_int_equal(Kit_FindBytes(AV_SAMPLE_FMT_U8P), 1);
    assert_int_equal(Kit_FindBytes(AV_SAMPLE_FMT_S32), 4);
    assert_int_equal(Kit_FindBytes(AV_SAMPLE_FMT_S32P), 4);
    assert_int_equal(Kit_FindBytes(AV_SAMPLE_FMT_S16), 2);
    assert_int_equal(Kit_FindBytes(AV_SAMPLE_FMT_FLT), 2);
}

/**
 * @brief Signed vs. unsigned classification matches each format's actual representation.
 */
static void test_find_signedness(void **state) {
    (void)state;
    // Arrange / Act / Assert
    assert_int_equal(Kit_FindSignedness(AV_SAMPLE_FMT_U8), 0);
    assert_int_equal(Kit_FindSignedness(AV_SAMPLE_FMT_U8P), 0);
    assert_int_equal(Kit_FindSignedness(AV_SAMPLE_FMT_S16), 1);
    assert_int_equal(Kit_FindSignedness(AV_SAMPLE_FMT_S32), 1);
}

/**
 * @brief AV sample formats map back to an SDL format; formats with no direct match (FLTP) degrade to a supported one.
 */
static void test_find_sdl_sample_format(void **state) {
    (void)state;
    // Arrange / Act / Assert: direct mappings, plus the no-direct-match degradation
    assert_int_equal(Kit_FindSDLSampleFormat(AV_SAMPLE_FMT_U8), AUDIO_U8);
    assert_int_equal(Kit_FindSDLSampleFormat(AV_SAMPLE_FMT_S32), AUDIO_S32SYS);
    assert_int_equal(Kit_FindSDLSampleFormat(AV_SAMPLE_FMT_S16), AUDIO_S16SYS);
    assert_int_equal(Kit_FindSDLSampleFormat(AV_SAMPLE_FMT_FLTP), AUDIO_S16SYS);
}

/**
 * @brief AVChannelLayout values for common speaker configurations classify into the matching Kit_AudioChannelLayout
 * enum.
 */
static void test_find_channel_layout(void **state) {
    (void)state;
    // Arrange
    AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout surround51 = AV_CHANNEL_LAYOUT_5POINT1_BACK;
    AVChannelLayout surround71 = AV_CHANNEL_LAYOUT_7POINT1;

    // Act / Assert
    assert_int_equal(Kit_FindChannelLayout(&mono), KIT_LAYOUT_MONO);
    assert_int_equal(Kit_FindChannelLayout(&stereo), KIT_LAYOUT_STEREO);
    assert_int_equal(Kit_FindChannelLayout(&surround51), KIT_LAYOUT_5POINT1);
    assert_int_equal(Kit_FindChannelLayout(&surround71), KIT_LAYOUT_7POINT1);
}

/**
 * @brief Every Kit_AudioChannelLayout round-trips through Kit_FindAVChannelLayout -> Kit_FindChannelLayout unchanged.
 */
static void test_layout_round_trip(void **state) {
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
    AVChannelLayout av_layout;

    // Act / Assert: each layout round-trips unchanged
    for(size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
        Kit_FindAVChannelLayout(layouts[i], &av_layout);
        assert_int_equal(Kit_FindChannelLayout(&av_layout), layouts[i]);
    }
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_find_av_sample_format),
        cmocka_unit_test(test_find_bytes),
        cmocka_unit_test(test_find_signedness),
        cmocka_unit_test(test_find_sdl_sample_format),
        cmocka_unit_test(test_find_channel_layout),
        cmocka_unit_test(test_layout_round_trip),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
