/**
 * Parametrized audio format matrix: sample format / channel / rate combos must
 * decode to non-silent PCM, and Kit_AudioFormatRequest overrides must be
 * reflected in the negotiated Kit_AudioOutputFormat. Needs the
 * committed KIT_TEST_DATA_DIR fixtures (test-data/media).
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "kit_lifecycle.h"
#include "kit_param.h"
#include "kit_playback.h"

#include <SDL.h>

#include "kitchensink2/kitchensink.h"

#define VIDEO_FILE KIT_TEST_DATA_DIR "/video_audio.mp4"

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or let a failed case's live player threads
 * cascade into (and leak across) the remaining cases in the group. `param` preserves the
 * kit_param_test case struct that cmocka handed in as the initial state. */
typedef struct {
    const void *param;
    Kit_Source *src;
    Kit_Player *player;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always
 * receives, capturing the kit_param_test case pointer cmocka passed as the initial state. */
static int test_setup(void **state) {
    const void *param = *state;
    TestState *ts = calloc(1, sizeof(TestState));
    if(ts == NULL)
        return -1;
    ts->param = param;
    *state = ts;
    return 0;
}

/** @brief Per-test teardown: releases whatever the TestState still holds (player first, since
 * Kit_ClosePlayer joins its threads), then the state itself. Tests NULL each member right after
 * their own close, so only what an assert-longjmp left behind is released here. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    Kit_ClosePlayer(ts->player);
    Kit_CloseSource(ts->src);
    free(ts);
    *state = NULL;
    return 0;
}

// -- test_audio_format_decodes -----------------------------------------

typedef struct {
    const char *label;     // case name and fixture file stem
    const char *file;      // full fixture path
    int expected_channels; // via Kit_GetChannelLayoutCount(output.layout)
    int min_sample_rate;   // sanity floor for the negotiated output rate
} AudioFormatCase;

/**
 * @brief Fixtures share a mono sine=440 lavfi source (audio_5point1.mka fans it to
 * 5.1). Kit_CreateAudioDecoder() never downmixes/upmixes, so expected_channels
 * tracks the source; min_sample_rate is the negotiated output rate (libopus
 * always resamples to 48000).
 */
static const AudioFormatCase decode_cases[] = {
    {"s16_wav",     KIT_TEST_DATA_DIR "/audio_s16.wav",     1, 44100},
    {"f32_wav",     KIT_TEST_DATA_DIR "/audio_f32.wav",     1, 44100},
    {"s32_flac",    KIT_TEST_DATA_DIR "/audio_s32.flac",    1, 44100},
    {"mp3",         KIT_TEST_DATA_DIR "/audio_mp3.mp3",     1, 44100},
    {"vorbis_ogg",  KIT_TEST_DATA_DIR "/audio_vorbis.ogg",  1, 44100},
    {"opus_mka",    KIT_TEST_DATA_DIR "/audio_opus.mka",    1, 48000},
    {"mono_m4a",    KIT_TEST_DATA_DIR "/audio_mono.m4a",    1, 44100},
    {"5point1_mka", KIT_TEST_DATA_DIR "/audio_5point1.mka", 6, 44100},
    {"22050_m4a",   KIT_TEST_DATA_DIR "/audio_22050.m4a",   1, 22050},
};

/**
 * @brief Every supported sample format / channel / rate combo decodes to non-silent PCM.
 */
static void test_audio_format_decodes(void **state) {
    TestState *ts = *state;
    const AudioFormatCase *c = ts->param;

    // Arrange
    ts->src = Kit_CreateSourceFromUrl(c->file);
    assert_non_null(ts->src);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    assert_true(audio_index >= 0);
    ts->player = Kit_CreatePlayer(ts->src, -1, audio_index, -1, NULL, NULL, 0, 0, NULL);
    assert_non_null(ts->player);

    Kit_PlayerInfo info;
    Kit_GetPlayerInfo(ts->player, &info);
    assert_int_equal(Kit_GetChannelLayoutCount(info.audio_format.layout), c->expected_channels);
    assert_true(info.audio_format.sample_rate >= c->min_sample_rate);

    // Act: wait for the audio output buffer to fill, then pump until data
    // arrives (bounded loop, ~2s worst case). The fill-rate wait doubles as
    // the suite's only exercise of Kit_WaitBufferFillRate()'s audio_output
    // percentage parameter.
    unsigned char buffer[8192];
    Kit_PlayerPlay(ts->player);
    assert_int_equal(Kit_WaitBufferFillRate(ts->player, -1, 10, -1, -1, 5.0), 0);
    int received = wait_for_audio_data(ts->player, buffer, sizeof(buffer));

    // The aac-encoded 5.1 fan-out (all channels copied from one mono sine
    // source) can start with a run of near-zero samples depending on frame
    // alignment; pump a couple more buffers before concluding on silence so
    // this case isn't flaky.
    bool nonzero = false;
    for(int extra = 0; extra < 5 && received > 0 && !nonzero; extra++) {
        for(int i = 0; i < received; i++)
            nonzero |= (buffer[i] != 0);
        if(!nonzero)
            received = wait_for_audio_data(ts->player, buffer, sizeof(buffer));
    }

    // Assert: got data, and it is not all zeroes (sine input must be audible)
    assert_true(received > 0);
    assert_true(nonzero);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_audio_format_request_honored ---------------------------------

typedef enum
{
    CHECK_FORMAT,
    CHECK_SAMPLE_RATE,
    CHECK_LAYOUT
} AudioRequestCheck;

typedef struct {
    const char *label;
    Kit_AudioFormatRequest request;
    AudioRequestCheck check;
    unsigned int expected_format;
    int expected_sample_rate;
    Kit_AudioChannelLayout expected_layout;
} AudioRequestCase;

/**
 * @brief Forcing one Kit_AudioFormatRequest field (format/sample_rate/layout) is reflected exactly in the negotiated
 * output.
 */
static void test_audio_format_request_honored(void **state) {
    TestState *ts = *state;
    const AudioRequestCase *c = ts->param;

    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    assert_true(audio_index >= 0);
    ts->player = Kit_CreatePlayer(ts->src, -1, audio_index, -1, NULL, &c->request, 0, 0, NULL);
    assert_non_null(ts->player);

    Kit_PlayerInfo info;
    Kit_GetPlayerInfo(ts->player, &info);

    // Act / Assert: the requested field must be honored exactly.
    switch(c->check) {
        case CHECK_FORMAT:
            assert_int_equal(info.audio_format.format, c->expected_format);
            break;
        case CHECK_SAMPLE_RATE:
            assert_int_equal(info.audio_format.sample_rate, c->expected_sample_rate);
            break;
        case CHECK_LAYOUT:
            assert_int_equal(info.audio_format.layout, c->expected_layout);
            assert_int_equal(Kit_GetChannelLayoutCount(info.audio_format.layout), 1);
            break;
    }

    // Requested output must still actually decode and produce audible data.
    unsigned char buffer[8192];
    Kit_PlayerPlay(ts->player);
    int received = wait_for_audio_data(ts->player, buffer, sizeof(buffer));
    assert_true(received > 0);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

int main(void) {
    KitParamName names[sizeof(decode_cases) / sizeof(decode_cases[0]) + 3];
    struct CMUnitTest tests[sizeof(decode_cases) / sizeof(decode_cases[0]) + 3];
    size_t n = 0;

    for(size_t i = 0; i < sizeof(decode_cases) / sizeof(decode_cases[0]); i++) {
        tests[n] = kit_param_test(
            &names[n],
            "test_audio_format_decodes",
            decode_cases[i].label,
            test_audio_format_decodes,
            test_setup,
            test_teardown,
            (void *)&decode_cases[i]
        );
        n++;
    }

    Kit_AudioFormatRequest req_format;
    Kit_ResetAudioFormatRequest(&req_format);
    req_format.format = AUDIO_S16SYS;

    Kit_AudioFormatRequest req_rate;
    Kit_ResetAudioFormatRequest(&req_rate);
    req_rate.sample_rate = 22050;

    Kit_AudioFormatRequest req_layout;
    Kit_ResetAudioFormatRequest(&req_layout);
    req_layout.layout = KIT_LAYOUT_MONO;

    const AudioRequestCase request_cases[] = {
        {"format_s16sys",     req_format, CHECK_FORMAT,      AUDIO_S16SYS, 0,     KIT_LAYOUT_UNKNOWN},
        {"sample_rate_22050", req_rate,   CHECK_SAMPLE_RATE, 0,            22050, KIT_LAYOUT_UNKNOWN},
        {"layout_mono",       req_layout, CHECK_LAYOUT,      0,            0,     KIT_LAYOUT_MONO   },
    };

    for(size_t i = 0; i < sizeof(request_cases) / sizeof(request_cases[0]); i++) {
        tests[n] = kit_param_test(
            &names[n],
            "test_audio_format_request_honored",
            request_cases[i].label,
            test_audio_format_request_honored,
            test_setup,
            test_teardown,
            (void *)&request_cases[i]
        );
        n++;
    }

    return cmocka_run_group_tests(tests, kit_lifecycle_setup, kit_lifecycle_teardown);
}
