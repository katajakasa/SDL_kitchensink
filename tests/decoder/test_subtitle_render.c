/**
 * End-to-end subtitle rendering: kitsubtitle.c, kitsubass.c, and the texture
 * atlas (src/internal/subtitle/kitatlas.c) in real use (see also
 * tests/unit/test_atlas.c for isolated atlas coverage). Rendering is
 * pull-driven off the player's sync clock, which only advances while
 * playback is pumped, so every test here must keep pumping the video texture
 * while polling the subtitle getter until rects appear. Needs
 * KIT_INIT_ASS.
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
#include <string.h>

#include "kit_assert.h"
#include "kit_lifecycle.h"
#include "kit_playback.h"

#include <SDL.h>

#include "kitchensink2/kitchensink.h"

#define SRT_FILE KIT_TEST_DATA_DIR "/subtitled.mkv"
#define ASS_FILE KIT_TEST_DATA_DIR "/subtitled_ass.mkv"
#define FONT_FILE KIT_TEST_DATA_DIR "/subtitled_font.mkv"
#define IMAGE_FILE KIT_TEST_DATA_DIR "/subtitled_image.mkv"

#define SCREEN_W 160
#define SCREEN_H 120
#define ATLAS_SIZE 512
#define RECT_LIMIT 16
#define PUMP_ITERS 300
#define PUMP_DELAY_MS 10

/** @brief Bundles everything needed to open a subtitled file against a headless renderer and pump it.
 * Heap-allocated per test by test_setup() and released by test_teardown(), so a mid-test assert
 * failure cannot leak it or let a failed test's live player threads cascade into (and leak
 * across) the remaining tests in the group. */
typedef struct {
    Kit_Source *src;
    Kit_Player *player;
    SDL_Surface *screen;
    SDL_Renderer *renderer;
    SDL_Texture *video_tex;
    SDL_Texture *sub_tex;
} SubtitleFixture;

/** @brief Per-test setup: heap-allocates the zeroed SubtitleFixture that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(SubtitleFixture));
    return *state == NULL ? -1 : 0;
}

/** @brief Opens `file` as a video+subtitle player with a headless renderer and starts playback. */
static void open_subtitle_fixture(SubtitleFixture *f, const char *file) {
    memset(f, 0, sizeof(*f));

    f->src = Kit_CreateSourceFromUrl(file);
    assert_non_null(f->src);
    const int video_index = Kit_GetBestSourceStream(f->src, KIT_STREAMTYPE_VIDEO);
    const int subtitle_index = Kit_GetBestSourceStream(f->src, KIT_STREAMTYPE_SUBTITLE);
    assert_true(video_index >= 0);
    assert_true(subtitle_index >= 0);

    // Subtitles need an open video stream to render against (per
    // Kit_SetPlayerStream()'s doc comment); no audio stream is selected
    // since these tests only exercise video+subtitle.
    f->player = Kit_CreatePlayer(f->src, video_index, -1, subtitle_index, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    assert_non_null(f->player);

    create_headless_renderer(SCREEN_W, SCREEN_H, &f->screen, &f->renderer);

    f->video_tex = Kit_CreatePlayerVideoSDLTexture(f->player, f->renderer, 0, 0);
    assert_non_null(f->video_tex);

    // Atlas texture: must be created via Kit_CreatePlayerSubtitleSDLTexture()
    // per its doc comment (kitplayer.h) to get the mandatory RGBA32 format /
    // STATIC access / nearest scale mode. Sized generously above the 160x120
    // video frame so worst-case subtitle fragments are never dropped.
    f->sub_tex = Kit_CreatePlayerSubtitleSDLTexture(f->player, f->renderer, ATLAS_SIZE, ATLAS_SIZE);
    assert_non_null(f->sub_tex);

    Kit_PlayerPlay(f->player);
    assert_int_equal(Kit_WaitBufferFillRate(f->player, -1, -1, -1, 50, 5.0), 0);
}

/** @brief Tears down a SubtitleFixture opened by open_subtitle_fixture(). Members are closed
 * only if created, so a partially built fixture (assert failure mid-open) closes cleanly;
 * zeroed afterwards, so the teardown's second close of an already-closed fixture is a no-op. */
static void close_subtitle_fixture(SubtitleFixture *f) {
    if(f->player != NULL) {
        Kit_PlayerStop(f->player);
        Kit_ClosePlayer(f->player);
    }
    if(f->sub_tex != NULL)
        SDL_DestroyTexture(f->sub_tex);
    if(f->video_tex != NULL)
        SDL_DestroyTexture(f->video_tex);
    if(f->renderer != NULL)
        SDL_DestroyRenderer(f->renderer);
    if(f->screen != NULL)
        SDL_FreeSurface(f->screen);
    if(f->src != NULL)
        Kit_CloseSource(f->src);
    memset(f, 0, sizeof(*f));
}

/** @brief Per-test teardown: closes whatever the fixture still holds -- including everything a
 * mid-test assert failure left open, so its live player threads cannot cascade into (and leak
 * across) the remaining tests in the group -- and frees the fixture itself. */
static int test_teardown(void **state) {
    SubtitleFixture *f = *state;
    if(f == NULL)
        return 0;
    close_subtitle_fixture(f);
    free(f);
    *state = NULL;
    return 0;
}

/** @brief Asserts a batch of rects has sources within the atlas bounds and targets with positive, sane sizes. */
static void assert_rects_sane(const SDL_Rect *sources, const SDL_Rect *targets, int count) {
    for(int i = 0; i < count; i++) {
        assert_rect_in_bounds(&sources[i], ATLAS_SIZE, ATLAS_SIZE);
        // Targets may extend past the screen area, so only their sizes are
        // bounded (against garbage values, not any real layout limit).
        assert_int_in_range(targets[i].w, 1, 10000);
        assert_int_in_range(targets[i].h, 1, 10000);
    }
}

/**
 * @brief SRT subtitles demux, decode, and render sane rects into the atlas texture.
 */
static void test_srt_subtitle_renders(void **state) {
    SubtitleFixture *f = *state;

    // Arrange
    open_subtitle_fixture(f, SRT_FILE);

    // Act
    SDL_Rect sources[RECT_LIMIT];
    SDL_Rect targets[RECT_LIMIT];
    const int got = pump_until_subtitle_rects(f->player, f->video_tex, f->sub_tex, sources, targets, RECT_LIMIT);

    // Assert
    assert_true(got > 0);
    assert_rects_sane(sources, targets, got);

    close_subtitle_fixture(f);
}

/**
 * @brief Native ASS subtitles demux, decode, and render sane rects into the atlas texture.
 */
static void test_ass_subtitle_renders(void **state) {
    SubtitleFixture *f = *state;

    // Arrange
    open_subtitle_fixture(f, ASS_FILE);

    // Act
    SDL_Rect sources[RECT_LIMIT];
    SDL_Rect targets[RECT_LIMIT];
    const int got = pump_until_subtitle_rects(f->player, f->video_tex, f->sub_tex, sources, targets, RECT_LIMIT);

    // Assert
    assert_true(got > 0);
    assert_rects_sane(sources, targets, got);

    close_subtitle_fixture(f);
}

/**
 * @brief Bitmap (dvd_subtitle) subtitles route through the image renderer (kitsubimage.c) and
 * render sane rects into the atlas texture -- the only test coverage that renderer has.
 */
static void test_image_subtitle_renders(void **state) {
    SubtitleFixture *f = *state;

    // Arrange
    open_subtitle_fixture(f, IMAGE_FILE);

    // Act
    SDL_Rect sources[RECT_LIMIT];
    SDL_Rect targets[RECT_LIMIT];
    const int got = pump_until_subtitle_rects(f->player, f->video_tex, f->sub_tex, sources, targets, RECT_LIMIT);

    // Assert
    assert_true(got > 0);
    assert_rects_sane(sources, targets, got);

    close_subtitle_fixture(f);
}

/**
 * @brief The bitmap cue in subtitled_image.mkv is a solid opaque red (#ff0000) rectangle
 * (see test-data/src/make_vobsub.py), and it must still be red after the palette
 * conversion in kitsubimage.c. Guards against reading FFmpeg's native-endian ARGB
 * palette words as byte-ordered SDL_Color fields, which swaps red and blue on
 * little-endian (issue #120).
 */
static void test_image_subtitle_colors(void **state) {
    SubtitleFixture *f = *state;

    // Arrange
    open_subtitle_fixture(f, IMAGE_FILE);

    SDL_Rect sources[RECT_LIMIT];
    SDL_Rect targets[RECT_LIMIT];
    const int got = pump_until_subtitle_rects(f->player, f->video_tex, f->sub_tex, sources, targets, RECT_LIMIT);
    assert_true(got > 0);

    // Act: draw the first cue fragment 1:1 into the top-left corner of the
    // screen surface and sample the pixel at its center.
    const SDL_Rect dst = {0, 0, sources[0].w, sources[0].h};
    assert_int_equal(SDL_SetRenderDrawColor(f->renderer, 0, 0, 0, 255), 0);
    assert_int_equal(SDL_RenderClear(f->renderer), 0);
    assert_int_equal(SDL_RenderCopy(f->renderer, f->sub_tex, &sources[0], &dst), 0);
    SDL_RenderPresent(f->renderer);

    // Assert
    Uint8 r, g, b, a;
    const Uint8 *row = (const Uint8 *)f->screen->pixels + (dst.h / 2) * f->screen->pitch;
    const Uint32 pixel = ((const Uint32 *)row)[dst.w / 2];
    SDL_GetRGBA(pixel, f->screen->format, &r, &g, &b, &a);
    assert_int_equal(r, 0xFF);
    assert_int_equal(g, 0x00);
    assert_int_equal(b, 0x00);
    assert_int_equal(a, 0xFF);

    close_subtitle_fixture(f);
}

/**
 * @brief Kit_SetPlayerScreenSize() on an image-subtitle player recomputes scaling without
 * crashing and keeps returning sane rects (guards the div-by-zero fix in ren_set_img_size_cb).
 */
static void test_image_subtitle_screen_resize(void **state) {
    SubtitleFixture *f = *state;

    // Arrange: get the cue on screen first.
    open_subtitle_fixture(f, IMAGE_FILE);

    SDL_Rect sources[RECT_LIMIT];
    SDL_Rect targets[RECT_LIMIT];
    const int got_before =
        pump_until_subtitle_rects(f->player, f->video_tex, f->sub_tex, sources, targets, RECT_LIMIT);
    assert_true(got_before > 0);

    // Act
    Kit_SetPlayerScreenSize(f->player, SCREEN_W * 2, SCREEN_H * 2);

    // Assert: the still-active cue keeps rendering sane rects at the new size.
    const int got_after = pump_until_subtitle_rects(f->player, f->video_tex, f->sub_tex, sources, targets, RECT_LIMIT);
    assert_true(got_after > 0);
    assert_rects_sane(sources, targets, got_after);

    close_subtitle_fixture(f);
}

/**
 * @brief After Kit_SetPlayerScreenSize(), the same still-active cue's rendered target rect changes to match the new
 * scale.
 */
static void test_subtitle_screen_resize(void **state) {
    SubtitleFixture *f = *state;

    // Arrange: render one cue at the original screen size.
    open_subtitle_fixture(f, SRT_FILE);

    SDL_Rect sources[RECT_LIMIT];
    SDL_Rect targets[RECT_LIMIT];
    const int got_before =
        pump_until_subtitle_rects(f->player, f->video_tex, f->sub_tex, sources, targets, RECT_LIMIT);
    assert_true(got_before > 0);
    const SDL_Rect target_before = targets[0];

    // Act
    Kit_SetPlayerScreenSize(f->player, SCREEN_W * 2, SCREEN_H * 2);

    // Keep polling until a returned target rect differs from the pre-resize
    // capture (bounded budget; the renderer may need a few iterations
    // before libass reports a layout change for the same cue).
    bool changed = false;
    for(int i = 0; i < PUMP_ITERS && !changed; i++) {
        Kit_GetPlayerVideoSDLTexture(f->player, f->video_tex, NULL);
        const int got_after = Kit_GetPlayerSubtitleSDLTexture(f->player, f->sub_tex, sources, targets, RECT_LIMIT);
        if(got_after > 0 && memcmp(&targets[0], &target_before, sizeof(SDL_Rect)) != 0)
            changed = true;
        else
            SDL_Delay(PUMP_DELAY_MS);
    }
    assert_true(changed);

    close_subtitle_fixture(f);
}

/**
 * @brief Kit_GetPlayerSubtitleRawFrames() returns non-null item/rect arrays with sane sizes.
 */
static void test_subtitle_raw_frames(void **state) {
    SubtitleFixture *f = *state;

    // Arrange
    open_subtitle_fixture(f, SRT_FILE);

    // Act
    unsigned char **items = NULL;
    SDL_Rect *sources = NULL;
    SDL_Rect *targets = NULL;
    int got = 0;
    for(int i = 0; i < PUMP_ITERS && got <= 0; i++) {
        Kit_GetPlayerVideoSDLTexture(f->player, f->video_tex, NULL);
        got = Kit_GetPlayerSubtitleRawFrames(f->player, &items, &sources, &targets);
        if(got <= 0)
            SDL_Delay(PUMP_DELAY_MS);
    }

    // Assert
    assert_true(got > 0);
    assert_non_null(items);
    assert_non_null(sources);
    assert_non_null(targets);
    for(int i = 0; i < got; i++) {
        assert_non_null(items[i]);
        assert_true(sources[i].w > 0);
        assert_true(sources[i].h > 0);
        assert_true(targets[i].w > 0);
        assert_true(targets[i].h > 0);
    }

    close_subtitle_fixture(f);
}

// -- test_font_attachment_subtitle -------------------------------------

/**
 * @brief A font attachment stream is skipped over (not treated as a decode failure) while its ASS cue still renders.
 */
static void test_font_attachment_subtitle(void **state) {
    SubtitleFixture *f = *state;

    // Arrange
    open_subtitle_fixture(f, FONT_FILE);

    // Act
    SDL_Rect sources[RECT_LIMIT];
    SDL_Rect targets[RECT_LIMIT];
    const int got = pump_until_subtitle_rects(f->player, f->video_tex, f->sub_tex, sources, targets, RECT_LIMIT);

    // Assert
    assert_true(got > 0);
    assert_rects_sane(sources, targets, got);

    close_subtitle_fixture(f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_srt_subtitle_renders, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_ass_subtitle_renders, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_image_subtitle_renders, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_image_subtitle_colors, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_image_subtitle_screen_resize, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_subtitle_screen_resize, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_subtitle_raw_frames, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_font_attachment_subtitle, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, kit_lifecycle_setup_video_ass, kit_lifecycle_teardown_video);
}
