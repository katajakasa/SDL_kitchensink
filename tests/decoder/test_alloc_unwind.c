/**
 * Constructor-unwind sweeps for Kit_CreatePlayer/Kit_CreateSourceFromUrl/
 * Kit_CreateSourceFromCustom via the "alloc" fault point (kitalloc.h):
 * probes each constructor's allocation count once unarmed, then fails each
 * ordinal in turn and checks for a clean NULL + Kit_GetError(), with ASan as
 * the leak detector. Built only when KIT_FAULT_INJECTION is enabled; the
 * #else branch keeps the binary buildable/runnable (empty) otherwise.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#ifdef KIT_FAULT_INJECTION

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kit_faultsweep.h"
#include "kit_lifecycle.h"
#include "kit_memsource.h"

#include "kitchensink2/internal/kitfaultinject.h"
#include "kitchensink2/kitchensink.h"

#define VIDEO_FILE KIT_TEST_DATA_DIR "/video_audio.mp4"
#define SUBTITLED_FILE KIT_TEST_DATA_DIR "/subtitled.mkv"
#define SUBTITLED_IMAGE_FILE KIT_TEST_DATA_DIR "/subtitled_image.mkv"

/**
 * @brief Sanity bound on the number of "alloc" ordinals a single constructor call
 * can consume; a real probe count is expected to land far below this.
 */
#define MAX_ALLOC_ORDINALS 200

/** @brief Test lifecycle setup: reset fail points and initialize the library. */
static int group_setup(void **state) {
    Kit_ResetFailPoints();
    return kit_lifecycle_setup_video_ass(state);
}

/** @brief Test lifecycle teardown: shut down the library and reset fail points. */
static int group_teardown(void **state) {
    kit_lifecycle_teardown_video(state);
    Kit_ResetFailPoints();
    return 0;
}

/** @brief Per-test sweep fixtures, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them across the remaining tests in the group. The
 * sources, memory buffer, and stream indices are the probe/attempt helpers' sweep context. */
typedef struct {
    Kit_Source *player_src;
    Kit_Source *sub_src;
    unsigned char *mem_data;
    int64_t mem_size;
    int player_video_index;
    int player_audio_index;
    int sub_video_index;
    int sub_subtitle_index;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: disarms all fail points even when the test body failed mid-way, so an
 * armed point cannot leak into (and cascade through) the remaining tests in the group; then releases
 * whatever the TestState still holds (with the points already disarmed, so the closes are clean),
 * then the state itself. Tests NULL each member right after their own close, so only what an
 * assert-longjmp left behind is released here. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    Kit_ResetFailPoints();
    Kit_CloseSource(ts->player_src);
    Kit_CloseSource(ts->sub_src);
    free(ts->mem_data);
    free(ts);
    *state = NULL;
    return 0;
}

// -- Kit_CreatePlayer --------------------------------------------------

/** @brief Creates a player from the TestState's fixture source and immediately closes it, for probing. */
static void create_and_close_player(void *ctx) {
    TestState *ts = ctx;
    Kit_Player *player = Kit_CreatePlayer(
        ts->player_src, ts->player_video_index, ts->player_audio_index, -1, NULL, NULL, 160, 120, NULL
    );
    if(player != NULL) {
        Kit_ClosePlayer(player);
    }
}

/** @brief Sweep attempt: player creation from the TestState's fixture source must fail cleanly. */
static void attempt_create_player(void *ctx) {
    TestState *ts = ctx;
    Kit_Player *player = Kit_CreatePlayer(
        ts->player_src, ts->player_video_index, ts->player_audio_index, -1, NULL, NULL, 160, 120, NULL
    );
    assert_null(player);
}

/**
 * @brief Failing the "alloc" point at each ordinal Kit_CreatePlayer() consumes yields a
 * clean NULL + Kit_GetError(), with no leaks (ASan-checked unwind).
 */
static void test_create_player_alloc_unwind(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->player_src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->player_src);
    ts->player_video_index = Kit_GetBestSourceStream(ts->player_src, KIT_STREAMTYPE_VIDEO);
    ts->player_audio_index = Kit_GetBestSourceStream(ts->player_src, KIT_STREAMTYPE_AUDIO);
    assert_true(ts->player_video_index >= 0);
    assert_true(ts->player_audio_index >= 0);

    const int ordinals = kit_probe_fail_point_count("alloc", create_and_close_player, ts);
    assert_int_in_range(ordinals, 1, MAX_ALLOC_ORDINALS);

    // Act / Assert: fail each ordinal in turn; every allocation failure ordinal must surface an error message.
    assert_int_equal(kit_sweep_fail_point("alloc", ordinals, attempt_create_player, ts), 0);

    Kit_CloseSource(ts->player_src);
    ts->player_src = NULL;
}

// -- Kit_CreateSourceFromUrl --------------------------------------------

/** @brief Creates a source from the fixture file and immediately closes it, for probing. */
static void create_and_close_source_from_url(void *ctx) {
    (void)ctx;
    Kit_Source *src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    if(src != NULL) {
        Kit_CloseSource(src);
    }
}

/** @brief Sweep attempt: source creation must fail cleanly with an error message. */
static void attempt_create_source_from_url(void *ctx) {
    (void)ctx;
    Kit_Source *src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_null(src);
    assert_non_null(Kit_GetError());
}

/**
 * @brief Failing the "alloc" point at each ordinal Kit_CreateSourceFromUrl() consumes yields a
 * clean NULL + Kit_GetError(), with no leaks (ASan-checked unwind).
 */
static void test_create_source_alloc_unwind(void **state) {
    TestState *ts = *state;
    // Arrange
    const int ordinals = kit_probe_fail_point_count("alloc", create_and_close_source_from_url, ts);
    assert_int_in_range(ordinals, 1, MAX_ALLOC_ORDINALS);

    // Act / Assert: fail each ordinal in turn (per-ordinal checks live in the attempt).
    kit_sweep_fail_point("alloc", ordinals, attempt_create_source_from_url, ts);
}

// -- Kit_CreateSourceFromCustom ------------------------------------------

/** @brief Creates a source from the TestState's in-memory fixture and immediately closes it, for probing. */
static void create_and_close_source_from_custom(void *ctx) {
    TestState *ts = ctx;
    MemFile mem = {.data = ts->mem_data, .size = ts->mem_size, .pos = 0};
    Kit_Source *src = Kit_CreateSourceFromCustom(mem_read, mem_seek, &mem);
    if(src != NULL) {
        Kit_CloseSource(src);
    }
}

/** @brief Sweep attempt: custom-IO source creation must fail cleanly with an error message. */
static void attempt_create_source_from_custom(void *ctx) {
    TestState *ts = ctx;
    MemFile mem = {.data = ts->mem_data, .size = ts->mem_size, .pos = 0};
    Kit_Source *src = Kit_CreateSourceFromCustom(mem_read, mem_seek, &mem);
    assert_null(src);
    assert_non_null(Kit_GetError());
}

/**
 * @brief Failing the "alloc" point at each ordinal Kit_CreateSourceFromCustom() consumes yields a
 * clean NULL + Kit_GetError(), with no leaks (ASan-checked unwind).
 */
static void test_create_source_custom_alloc_unwind(void **state) {
    TestState *ts = *state;
    // Arrange
    assert_int_equal(load_file(VIDEO_FILE, &ts->mem_data, &ts->mem_size), 0);

    const int ordinals = kit_probe_fail_point_count("alloc", create_and_close_source_from_custom, ts);
    assert_int_in_range(ordinals, 1, MAX_ALLOC_ORDINALS);

    // Act / Assert: fail each ordinal in turn (per-ordinal checks live in the attempt).
    kit_sweep_fail_point("alloc", ordinals, attempt_create_source_from_custom, ts);

    free(ts->mem_data);
    ts->mem_data = NULL;
}

// -- Kit_CreatePlayer (subtitle) ----------------------------------------

/** @brief Creates a video+subtitle player from the TestState's sub_src and immediately closes it, for probing. */
static void create_and_close_subtitle_player(void *ctx) {
    TestState *ts = ctx;
    Kit_Player *player =
        Kit_CreatePlayer(ts->sub_src, ts->sub_video_index, -1, ts->sub_subtitle_index, NULL, NULL, 160, 120, NULL);
    if(player != NULL) {
        Kit_ClosePlayer(player);
    }
}

/** @brief Sweep attempt: video+subtitle player creation must fail cleanly with an error message. */
static void attempt_create_subtitle_player(void *ctx) {
    TestState *ts = ctx;
    Kit_Player *player =
        Kit_CreatePlayer(ts->sub_src, ts->sub_video_index, -1, ts->sub_subtitle_index, NULL, NULL, 160, 120, NULL);
    assert_null(player);
    assert_non_null(Kit_GetError());
}

/** @brief Shared body: sweeps every "alloc" ordinal of a video+subtitle player built from `file`. */
static void sweep_subtitle_player_allocs(TestState *ts, const char *file) {
    // Arrange
    ts->sub_src = Kit_CreateSourceFromUrl(file);
    assert_non_null(ts->sub_src);
    ts->sub_video_index = Kit_GetBestSourceStream(ts->sub_src, KIT_STREAMTYPE_VIDEO);
    ts->sub_subtitle_index = Kit_GetBestSourceStream(ts->sub_src, KIT_STREAMTYPE_SUBTITLE);
    assert_true(ts->sub_video_index >= 0);
    assert_true(ts->sub_subtitle_index >= 0);

    const int ordinals = kit_probe_fail_point_count("alloc", create_and_close_subtitle_player, ts);
    assert_int_in_range(ordinals, 1, MAX_ALLOC_ORDINALS);

    // Act / Assert: fail each ordinal in turn (per-ordinal checks live in the attempt).
    kit_sweep_fail_point("alloc", ordinals, attempt_create_subtitle_player, ts);

    Kit_CloseSource(ts->sub_src);
    ts->sub_src = NULL;
}

/**
 * @brief Failing each "alloc" ordinal of an ASS-subtitle player creation yields a clean
 * NULL + Kit_GetError(), with no leaks or double frees (ASan-checked unwind of
 * Kit_CreateASSSubtitleRenderer's error paths).
 */
static void test_create_ass_subtitle_player_alloc_unwind(void **state) {
    TestState *ts = *state;
    sweep_subtitle_player_allocs(ts, SUBTITLED_FILE);
}

/**
 * @brief Failing each "alloc" ordinal of a bitmap-subtitle player creation yields a clean
 * NULL + Kit_GetError(), with no leaks or double frees (ASan-checked unwind of
 * Kit_CreateImageSubtitleRenderer's error paths).
 */
static void test_create_image_subtitle_player_alloc_unwind(void **state) {
    TestState *ts = *state;
    sweep_subtitle_player_allocs(ts, SUBTITLED_IMAGE_FILE);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_create_player_alloc_unwind, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_source_alloc_unwind, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_source_custom_alloc_unwind, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_ass_subtitle_player_alloc_unwind, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_image_subtitle_player_alloc_unwind, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}

#else

int main(void) {
    return 0;
}

#endif // KIT_FAULT_INJECTION
