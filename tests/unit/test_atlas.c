/**
 * Unit tests for Kit_TextureAtlas (kitatlas.h), which packs subtitle glyph/
 * bitmap surfaces into a shared texture. The group fixture drives a headless
 * SDL software renderer (no window, no GPU) so these tests run in CI without
 * a display.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "kitchensink3/internal/subtitle/kitatlas.h"

typedef struct atlas_env {
    SDL_Surface *screen;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
} atlas_env;

static int group_setup(void **state) {
    if(!SDL_Init(SDL_INIT_VIDEO))
        return -1;
    atlas_env *env = calloc(1, sizeof(atlas_env));
    if(env == NULL) {
        SDL_Quit(); // cmocka skips group teardown when setup fails
        return -1;
    }
    env->screen = SDL_CreateSurface(1024, 1024, SDL_PIXELFORMAT_RGBA32);
    env->renderer = SDL_CreateSoftwareRenderer(env->screen);
    env->texture = SDL_CreateTexture(env->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 1024, 1024);
    if(env->screen == NULL || env->renderer == NULL || env->texture == NULL) {
        if(env->texture != NULL)
            SDL_DestroyTexture(env->texture);
        if(env->renderer != NULL)
            SDL_DestroyRenderer(env->renderer);
        if(env->screen != NULL)
            SDL_DestroySurface(env->screen);
        free(env);
        SDL_Quit();
        return -1;
    }
    *state = env;
    return 0;
}

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or cascade into the remaining tests in the
 * group. env is the group fixture's render target, borrowed (never owned) by the test. */
typedef struct {
    atlas_env *env;
    Kit_TextureAtlas *atlas;
    SDL_Surface *item;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always
 * receives, keeping a borrowed pointer to the group's atlas_env. */
static int test_setup(void **state) {
    atlas_env *env = *state;
    TestState *ts = calloc(1, sizeof(TestState));
    if(ts == NULL)
        return -1;
    ts->env = env;
    *state = ts;
    return 0;
}

/** @brief Per-test teardown: releases whatever the TestState still holds, then the state
 * itself. Tests NULL each member right after their own close, so only what an assert-longjmp
 * left behind is released here. The atlas never owns item surfaces (Kit_AddAtlasItem only
 * copies their pixels into the texture), so both must be freed; Kit_FreeAtlas() is not
 * NULL-safe. env belongs to the group fixture and is left alone. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    if(ts->atlas != NULL)
        Kit_FreeAtlas(ts->atlas);
    if(ts->item != NULL)
        SDL_DestroySurface(ts->item);
    free(ts);
    *state = NULL;
    return 0;
}

static int group_teardown(void **state) {
    atlas_env *env = *state;
    SDL_DestroyTexture(env->texture);
    SDL_DestroyRenderer(env->renderer);
    SDL_DestroySurface(env->screen);
    free(env);
    SDL_Quit();
    return 0;
}

/**
 * @brief A freshly created atlas starts out empty.
 */
static void test_create_and_free(void **state) {
    TestState *ts = *state;
    // Arrange / Act
    ts->atlas = Kit_CreateAtlas();

    // Assert: a new atlas starts empty
    assert_non_null(ts->atlas);
    assert_int_equal(ts->atlas->cur_items, 0);
    Kit_FreeAtlas(ts->atlas);
    ts->atlas = NULL;
}

/**
 * @brief Kit_CheckAtlasTextureSize() picks up the backing texture's actual dimensions.
 */
static void test_check_texture_size(void **state) {
    TestState *ts = *state;
    atlas_env *env = ts->env;
    // Arrange
    ts->atlas = Kit_CreateAtlas();

    // Act
    Kit_CheckAtlasTextureSize(ts->atlas, env->texture);

    // Assert
    assert_int_equal(ts->atlas->w, 1024);
    assert_int_equal(ts->atlas->h, 1024);
    Kit_FreeAtlas(ts->atlas);
    ts->atlas = NULL;
}

/**
 * @brief Items added to the atlas are retrievable with their original source size and target position.
 */
static void test_add_and_get_items(void **state) {
    TestState *ts = *state;
    atlas_env *env = ts->env;
    // Arrange
    ts->atlas = Kit_CreateAtlas();
    Kit_CheckAtlasTextureSize(ts->atlas, env->texture);
    ts->item = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
    assert_non_null(ts->item);
    const SDL_FRect target = {10, 10, 16, 16};

    // Act
    assert_int_equal(Kit_AddAtlasItem(ts->atlas, env->texture, ts->item, &target), 0);

    // Assert: item was recorded and can be read back
    assert_int_equal(ts->atlas->cur_items, 1);
    SDL_FRect sources[4], targets[4];
    assert_int_equal(Kit_GetAtlasItems(ts->atlas, sources, targets, 4), 1);
    assert_float_equal(targets[0].x, 10.0f, 0.0);
    assert_float_equal(targets[0].y, 10.0f, 0.0);
    assert_float_equal(sources[0].w, 16.0f, 0.0);
    assert_float_equal(sources[0].h, 16.0f, 0.0);

    SDL_DestroySurface(ts->item);
    ts->item = NULL;
    Kit_FreeAtlas(ts->atlas);
    ts->atlas = NULL;
}

/**
 * @brief A fractional target rect survives the add/get round-trip unrounded, and the returned
 * source rect equals the packed integer region converted to float -- subpixel placement must not
 * be truncated anywhere in the atlas.
 */
static void test_fractional_target_roundtrip(void **state) {
    TestState *ts = *state;
    atlas_env *env = ts->env;
    // Arrange
    ts->atlas = Kit_CreateAtlas();
    Kit_CheckAtlasTextureSize(ts->atlas, env->texture);
    ts->item = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
    assert_non_null(ts->item);
    const SDL_FRect target = {10.5f, 20.25f, 16.75f, 8.5f};

    // Act
    assert_int_equal(Kit_AddAtlasItem(ts->atlas, env->texture, ts->item, &target), 0);

    // Assert: target comes back bit-exact, source is the integer packed region as float
    SDL_FRect sources[1], targets[1];
    assert_int_equal(Kit_GetAtlasItems(ts->atlas, sources, targets, 1), 1);
    assert_float_equal(targets[0].x, 10.5f, 0.0);
    assert_float_equal(targets[0].y, 20.25f, 0.0);
    assert_float_equal(targets[0].w, 16.75f, 0.0);
    assert_float_equal(targets[0].h, 8.5f, 0.0);
    assert_float_equal(sources[0].x, 0.0f, 0.0);
    assert_float_equal(sources[0].y, 0.0f, 0.0);
    assert_float_equal(sources[0].w, 16.0f, 0.0);
    assert_float_equal(sources[0].h, 16.0f, 0.0);

    SDL_DestroySurface(ts->item);
    ts->item = NULL;
    Kit_FreeAtlas(ts->atlas);
    ts->atlas = NULL;
}

/**
 * @brief Kit_ClearAtlasContent() resets the item count so a populated atlas can be reused for the next subtitle frame.
 */
static void test_clear_resets_atlas(void **state) {
    TestState *ts = *state;
    atlas_env *env = ts->env;
    // Arrange
    ts->atlas = Kit_CreateAtlas();
    Kit_CheckAtlasTextureSize(ts->atlas, env->texture);
    ts->item = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
    assert_non_null(ts->item);
    const SDL_FRect target = {10, 10, 16, 16};
    assert_int_equal(Kit_AddAtlasItem(ts->atlas, env->texture, ts->item, &target), 0);
    assert_int_equal(ts->atlas->cur_items, 1);

    // Act
    Kit_ClearAtlasContent(ts->atlas);

    // Assert
    assert_int_equal(ts->atlas->cur_items, 0);

    SDL_DestroySurface(ts->item);
    ts->item = NULL;
    Kit_FreeAtlas(ts->atlas);
    ts->atlas = NULL;
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_create_and_free, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_check_texture_size, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_add_and_get_items, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_fractional_target_roundtrip, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_clear_resets_atlas, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
