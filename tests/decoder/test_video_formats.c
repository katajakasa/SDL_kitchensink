/**
 * Parametrized video codec/pixel-format matrix: every supported input
 * codec/pixel-format/geometry combo must decode and land in an SDL texture
 * with the expected Kit_VideoOutputFormat.width/height. Needs the committed
 * KIT_TEST_DATA_DIR fixtures (test-data/media); headless SDL software
 * renderer.
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

#include "kitchensink3/kitchensink.h"

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or let a failed case's live player threads
 * cascade into (and leak across) the remaining cases in the group. `param` preserves the
 * kit_param_test case struct that cmocka handed in as the initial state. */
typedef struct {
    const void *param;
    Kit_Source *src;
    Kit_Player *player;
    SDL_Surface *screen;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
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
    if(ts->texture != NULL)
        SDL_DestroyTexture(ts->texture);
    if(ts->renderer != NULL)
        SDL_DestroyRenderer(ts->renderer);
    if(ts->screen != NULL)
        SDL_FreeSurface(ts->screen);
    Kit_CloseSource(ts->src);
    free(ts);
    *state = NULL;
    return 0;
}

/** @brief True if format is one Kit_FindSDLPixelFormat() can actually produce (1:1 YUV set, or its RGBA32 fallback).
 */
static bool is_supported_sdl_video_format(unsigned int format) {
    switch(format) {
        case SDL_PIXELFORMAT_YV12:
        case SDL_PIXELFORMAT_YUY2:
        case SDL_PIXELFORMAT_UYVY:
        case SDL_PIXELFORMAT_NV12:
        case SDL_PIXELFORMAT_NV21:
        case SDL_PIXELFORMAT_RGBA32:
            return true;
        default:
            return false;
    }
}

typedef struct {
    const char *label; // case name and fixture file stem
    const char *file;  // full fixture path
    int expected_w;
    int expected_h;
} VideoFormatCase;

static const VideoFormatCase decode_cases[] = {
    {"vp9",     KIT_TEST_DATA_DIR "/video_vp9.webm",    160, 120},
    {"10bit",   KIT_TEST_DATA_DIR "/video_10bit.mkv",   160, 120},
    {"mpeg2",   KIT_TEST_DATA_DIR "/video_mpeg2.ts",    160, 120},
    {"yuyv422", KIT_TEST_DATA_DIR "/video_yuyv422.nut", 160, 120},
    {"oddsize", KIT_TEST_DATA_DIR "/video_oddsize.nut", 177, 101},
    {"tiny",    KIT_TEST_DATA_DIR "/video_tiny.mp4",    16,  16 },
};

/**
 * @brief Every supported input codec/pixel-format/geometry combo decodes and lands in an SDL texture at the expected
 * size.
 */
static void test_video_format_decodes(void **state) {
    TestState *ts = *state;
    const VideoFormatCase *c = ts->param;

    // Arrange: open source, video-only player, and a headless render target
    // sized to the case's expected geometry.
    ts->src = Kit_CreateSourceFromUrl(c->file);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    assert_true(video_index >= 0);
    ts->player = Kit_CreatePlayer(ts->src, video_index, -1, -1, NULL, NULL, 0, 0, NULL);
    assert_non_null(ts->player);

    // Assert: negotiated output geometry/format matches the case.
    Kit_PlayerInfo info;
    Kit_GetPlayerInfo(ts->player, &info);
    assert_int_equal(info.video_format.width, c->expected_w);
    assert_int_equal(info.video_format.height, c->expected_h);
    assert_true(is_supported_sdl_video_format(info.video_format.format));

    ts->screen = NULL;
    ts->renderer = NULL;
    create_headless_renderer(c->expected_w, c->expected_h, &ts->screen, &ts->renderer);
    ts->texture = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
    assert_non_null(ts->texture);

    // Act: start playback, wait for the output buffer, then pull frames.
    Kit_PlayerPlay(ts->player);
    assert_int_equal(Kit_WaitBufferFillRate(ts->player, -1, -1, -1, 50, 5.0), 0);

    // Assert
    assert_true(wait_for_video_frame(ts->player, ts->texture));

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    SDL_DestroyTexture(ts->texture);
    ts->texture = NULL;
    SDL_DestroyRenderer(ts->renderer);
    ts->renderer = NULL;
    SDL_FreeSurface(ts->screen);
    ts->screen = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

int main(void) {
    KitParamName names[sizeof(decode_cases) / sizeof(decode_cases[0])];
    struct CMUnitTest tests[sizeof(decode_cases) / sizeof(decode_cases[0])];
    size_t n = 0;

    for(size_t i = 0; i < sizeof(decode_cases) / sizeof(decode_cases[0]); i++) {
        tests[n] = kit_param_test(
            &names[n],
            "test_video_format_decodes",
            decode_cases[i].label,
            test_video_format_decodes,
            test_setup,
            test_teardown,
            (void *)&decode_cases[i]
        );
        n++;
    }

    return cmocka_run_group_tests(tests, kit_lifecycle_setup_video, kit_lifecycle_teardown_video);
}
