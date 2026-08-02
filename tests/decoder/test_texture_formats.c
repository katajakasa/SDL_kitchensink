/**
 * Output texture format sweep: every SDL pixel format Kit_FindAVPixelFormat()
 * recognizes must be requestable via Kit_VideoFormatRequest.format and land
 * in an SDL texture of that format (including all four RGBA byte-order
 * aliases); a garbage/unhandled format must fail negotiation cleanly
 * instead of falling back to a default.
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
#define SUBTITLED_FILE KIT_TEST_DATA_DIR "/subtitled.mkv"

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or let a failed case's live player threads
 * cascade into (and leak across) the remaining cases in the group. `param` preserves the
 * kit_param_test case struct that cmocka handed in as the initial state; `pixels` is the
 * contrast helper's readback buffer, kept here for the same unwind reason. */
typedef struct {
    const void *param;
    Kit_Source *src;
    Kit_Player *player;
    SDL_Surface *screen;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    unsigned char *pixels;
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
    free(ts->pixels);
    Kit_CloseSource(ts->src);
    free(ts);
    *state = NULL;
    return 0;
}

/** @brief Reads texture back as RGBA8888 and asserts it is not a single flat color (catches channel-swap/stride bugs).
 * The readback buffer lives in ts->pixels so an assert-longjmp cannot leak it. */
static void assert_texture_has_contrast(TestState *ts, SDL_Renderer *renderer, SDL_Texture *texture) {
    int w = 0, h = 0;
    assert_int_equal(SDL_QueryTexture(texture, NULL, NULL, &w, &h), 0);

    assert_int_equal(SDL_SetRenderTarget(renderer, NULL), 0);
    assert_int_equal(SDL_RenderClear(renderer), 0);
    assert_int_equal(SDL_RenderCopy(renderer, texture, NULL, NULL), 0);

    ts->pixels = malloc((size_t)w * h * 4);
    assert_non_null(ts->pixels);
    assert_int_equal(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGBA32, ts->pixels, w * 4), 0);

    unsigned char min[4] = {255, 255, 255, 255};
    unsigned char max[4] = {0, 0, 0, 0};
    for(int i = 0; i < w * h; i++) {
        for(int c = 0; c < 4; c++) {
            unsigned char v = ts->pixels[i * 4 + c];
            if(v < min[c])
                min[c] = v;
            if(v > max[c])
                max[c] = v;
        }
    }
    free(ts->pixels);
    ts->pixels = NULL;

    bool has_contrast = false;
    for(int c = 0; c < 4; c++) {
        if((int)max[c] - (int)min[c] > 64)
            has_contrast = true;
    }
    assert_true(has_contrast);
}

// -- test_texture_format_honored ---------------------------------------

typedef struct {
    const char *label;   // case name
    unsigned int format; // requested/expected SDL_PIXELFORMAT_*
} TextureFormatCase;

static const TextureFormatCase format_cases[] = {
    {"yv12",     SDL_PIXELFORMAT_YV12  },
    {"iyuv",     SDL_PIXELFORMAT_IYUV  },
    {"yuy2",     SDL_PIXELFORMAT_YUY2  },
    {"uyvy",     SDL_PIXELFORMAT_UYVY  },
    {"yvyu",     SDL_PIXELFORMAT_YVYU  },
    {"nv12",     SDL_PIXELFORMAT_NV12  },
    {"nv21",     SDL_PIXELFORMAT_NV21  },
    {"argb32",   SDL_PIXELFORMAT_ARGB32},
    {"rgba32",   SDL_PIXELFORMAT_RGBA32},
    {"bgra32",   SDL_PIXELFORMAT_BGRA32},
    {"abgr32",   SDL_PIXELFORMAT_ABGR32},
    {"xrgb8888", SDL_PIXELFORMAT_RGB888},
    {"xbgr8888", SDL_PIXELFORMAT_BGR888},
    {"rgb24",    SDL_PIXELFORMAT_RGB24 },
    {"bgr24",    SDL_PIXELFORMAT_BGR24 },
    {"rgb555",   SDL_PIXELFORMAT_RGB555},
    {"bgr555",   SDL_PIXELFORMAT_BGR555},
    {"rgb565",   SDL_PIXELFORMAT_RGB565},
    {"bgr565",   SDL_PIXELFORMAT_BGR565},
};

/**
 * @brief Requested Kit_VideoFormatRequest.format is negotiated exactly and produces a decodable, correctly laid out
 * texture.
 */
static void test_texture_format_honored(void **state) {
    TestState *ts = *state;
    const TextureFormatCase *c = ts->param;

    // Arrange: request the case format explicitly.
    Kit_VideoFormatRequest req;
    Kit_ResetVideoFormatRequest(&req);
    req.format = c->format;

    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    assert_true(video_index >= 0);
    ts->player = Kit_CreatePlayer(ts->src, video_index, -1, -1, &req, NULL, 0, 0, NULL);
    assert_non_null(ts->player);

    // Assert: negotiated output format echoes the request exactly.
    Kit_PlayerInfo info;
    Kit_GetPlayerInfo(ts->player, &info);
    assert_int_equal(info.video_format.format, c->format);

    ts->screen = NULL;
    ts->renderer = NULL;
    create_headless_renderer(info.video_format.width, info.video_format.height, &ts->screen, &ts->renderer);
    ts->texture = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
    assert_non_null(ts->texture);

    // Act: start playback, pump until a frame lands in the texture.
    Kit_PlayerPlay(ts->player);
    assert_int_equal(Kit_WaitBufferFillRate(ts->player, -1, -1, -1, 50, 5.0), 0);
    assert_true(wait_for_video_frame(ts->player, ts->texture));

    assert_texture_has_contrast(ts, ts->renderer, ts->texture);

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

// -- test_unsupported_format_request -----------------------------------

/**
 * @brief An unrecognized requested pixel format fails player creation cleanly instead of falling back to a default.
 */
static void test_unsupported_format_request(void **state) {
    TestState *ts = *state;
    // Arrange: request a format no Kit_FindAVPixelFormat() case handles.
    Kit_VideoFormatRequest req;
    Kit_ResetVideoFormatRequest(&req);
    req.format = 0xDEADBEEFu; // Not a valid SDL_PixelFormat value at all.

    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    assert_true(video_index >= 0);

    // Act / Assert: creation fails and sets an error.
    ts->player = Kit_CreatePlayer(ts->src, video_index, -1, -1, &req, NULL, 0, 0, NULL);
    assert_null(ts->player);
    assert_non_null(Kit_GetError());

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_subtitle_output_format ---------------------------------------

/**
 * @brief Subtitle output format is hard-coded to SDL_PIXELFORMAT_RGBA32, never negotiated.
 */
static void test_subtitle_output_format(void **state) {
    TestState *ts = *state;

    ts->src = Kit_CreateSourceFromUrl(SUBTITLED_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int subtitle_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE);
    assert_true(video_index >= 0);
    assert_true(subtitle_index >= 0);

    ts->player = Kit_CreatePlayer(ts->src, video_index, -1, subtitle_index, NULL, NULL, 160, 120, NULL);
    assert_non_null(ts->player);

    Kit_PlayerInfo info;
    Kit_GetPlayerInfo(ts->player, &info);
    assert_int_equal(info.subtitle_format.format, SDL_PIXELFORMAT_RGBA32);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

int main(void) {
    KitParamName names[sizeof(format_cases) / sizeof(format_cases[0])];
    struct CMUnitTest tests[sizeof(format_cases) / sizeof(format_cases[0]) + 2];
    size_t n = 0;

    for(size_t i = 0; i < sizeof(format_cases) / sizeof(format_cases[0]); i++) {
        tests[n] = kit_param_test(
            &names[n],
            "test_texture_format_honored",
            format_cases[i].label,
            test_texture_format_honored,
            test_setup,
            test_teardown,
            (void *)&format_cases[i]
        );
        n++;
    }

    tests[n++] = (struct CMUnitTest){
        "test_unsupported_format_request", test_unsupported_format_request, test_setup, test_teardown, NULL
    };
    tests[n++] = (struct CMUnitTest){
        "test_subtitle_output_format", test_subtitle_output_format, test_setup, test_teardown, NULL
    };

    return cmocka_run_group_tests(tests, kit_lifecycle_setup_video_ass, kit_lifecycle_teardown_video);
}
