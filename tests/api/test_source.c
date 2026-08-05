/**
 * Tests for Kit_CreateSourceFromUrl/Kit_CloseSource error paths and the
 * broader Kit_Source stream-introspection API (kitsource.h): stream lists,
 * next-stream iteration, IOStream/custom-IO source creation, and stream info
 * lookups.
 *
 * Needs the generated fixtures under KIT_TEST_DATA_DIR (video_audio.mp4,
 * many_subs.mkv, audio_first.mkv, no_duration.h264); see tests/CMakeLists.txt.
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

#include "kit_assert.h"
#include "kit_lifecycle.h"
#include "kit_memsource.h"

#include "kitchensink3/kitchensink.h"

#define VIDEO_AUDIO_FILE KIT_TEST_DATA_DIR "/video_audio.mp4"
#define MANY_SUBS_FILE KIT_TEST_DATA_DIR "/many_subs.mkv"
#define AUDIO_FIRST_FILE KIT_TEST_DATA_DIR "/audio_first.mkv"
#define NO_DURATION_FILE KIT_TEST_DATA_DIR "/no_duration.h264"

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or cascade into the remaining tests in the
 * group. mem lives here (not stack-local) because src's IO callbacks reference it for as
 * long as the source stays open -- including in the teardown's Kit_CloseSource(). */
typedef struct {
    Kit_Source *src;
    SDL_IOStream *io;
    unsigned char *data;
    MemFile mem;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: releases whatever the TestState still holds (source, IOStream, file
 * buffer), then the state itself. Tests NULL each member right after their own close, so only
 * what an assert-longjmp left behind is released here. The IOStream is never owned by the source
 * (Kit_CreateSourceFromIO only stores it as callback userdata), so closing it after the source
 * is always safe. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    Kit_CloseSource(ts->src);
    if(ts->io != NULL)
        SDL_CloseIO(ts->io);
    free(ts->data);
    free(ts);
    *state = NULL;
    return 0;
}

/**
 * @brief Opening a nonexistent path must return NULL and set Kit_GetError(), not crash.
 */
static void test_open_nonexistent_url_fails(void **state) {
    (void)state;
    assert_source_open_fails_cleanly("/nonexistent/no_such_file.mp4");
}

/**
 * @brief A NULL URL must be rejected like a bad path: NULL return plus an error message.
 */
static void test_open_null_url_fails(void **state) {
    (void)state;
    assert_source_open_fails_cleanly(NULL);
}

/**
 * @brief Kit_CloseSource(NULL) is documented as a valid no-op.
 */
static void test_close_null_source_is_noop(void **state) {
    (void)state;
    Kit_CloseSource(NULL); // documented as valid, must not crash
}

/**
 * @brief Kit_GetSourceDuration() preserves sub-second precision: the 2.023s fixture must not
 * read as an integer-truncated 2.0.
 */
static void test_source_duration_fractional(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(AUDIO_FIRST_FILE);
    assert_non_null(ts->src);

    // Act / Assert: integer division would yield exactly 2.0, below this range.
    assert_double_in_range(Kit_GetSourceDuration(ts->src), 2.01, 2.04);

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_GetSourceDuration() reports an unknown duration as exactly -1: a raw elementary
 * stream has no container duration, which must not leak through as a raw AV_NOPTS_VALUE
 * division (a garbage value in the billions of negative seconds).
 */
static void test_source_duration_unknown(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(NO_DURATION_FILE);
    assert_non_null(ts->src);

    // Act / Assert
    assert_double_in_range(Kit_GetSourceDuration(ts->src), -1.0, -1.0);

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_GetSourceStreamList() finds all matching streams, truncates on a too-small
 * buffer, and returns 0 when no stream of the requested type exists.
 */
static void test_stream_list(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(MANY_SUBS_FILE);
    assert_non_null(ts->src);
    int list[8] = {0};

    // Act / Assert: room for all three subtitle streams
    int count = Kit_GetSourceStreamList(ts->src, KIT_STREAMTYPE_SUBTITLE, list, 8);
    assert_int_equal(count, 3);
    for(int i = 0; i < 3; i++) {
        assert_true(list[i] >= 0);
    }

    // Act / Assert: only room for two -> truncates, but only two slots filled
    int small_list[3] = {-1, -1, -777};
    count = Kit_GetSourceStreamList(ts->src, KIT_STREAMTYPE_SUBTITLE, small_list, 2);
    assert_int_equal(count, 2);
    assert_true(small_list[0] >= 0);
    assert_true(small_list[1] >= 0);
    assert_int_equal(small_list[2], -777); // nothing written past the stated size

    // Act / Assert: no audio streams in this file
    count = Kit_GetSourceStreamList(ts->src, KIT_STREAMTYPE_AUDIO, list, 8);
    assert_int_equal(count, 0);

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_GetNextSourceStream() walks matching streams in index order, returns -1 at
 * the end without looping, and wraps to the first match when looping is requested.
 */
static void test_next_stream_iteration(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(MANY_SUBS_FILE);
    assert_non_null(ts->src);
    int list[8] = {0};
    int count = Kit_GetSourceStreamList(ts->src, KIT_STREAMTYPE_SUBTITLE, list, 8);
    assert_int_equal(count, 3);

    // Act / Assert: walk forward from before the first stream
    int first = Kit_GetNextSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE, -1, 0);
    assert_int_equal(first, list[0]);
    int second = Kit_GetNextSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE, first, 0);
    assert_int_equal(second, list[1]);
    int third = Kit_GetNextSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE, second, 0);
    assert_int_equal(third, list[2]);

    // Act / Assert: past the last stream without looping -> documented -1
    int past_end = Kit_GetNextSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE, third, 0);
    assert_int_equal(past_end, -1);

    // Act / Assert: past the last stream with looping -> wraps to the first
    int wrapped = Kit_GetNextSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE, third, 1);
    assert_int_equal(wrapped, list[0]);

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_CreateSourceFromIO() produces a fully usable source from an SDL_IOStream.
 * The IOStream outlives the source and must be closed separately afterwards.
 */
static void test_source_from_io(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->io = SDL_IOFromFile(VIDEO_AUDIO_FILE, "rb");
    assert_non_null(ts->io);

    // Act
    ts->src = Kit_CreateSourceFromIO(ts->io);

    // Assert
    assert_non_null(ts->src);
    assert_int_equal(Kit_GetSourceStreamCount(ts->src), 2);

    Kit_CloseSource(ts->src);
    ts->src = NULL;
    SDL_CloseIO(ts->io);
    ts->io = NULL;
}

/**
 * @brief Kit_CreateSourceFromCustom() works against arbitrary read/seek callbacks,
 * producing the same stream count and an approximately correct duration.
 */
static void test_source_from_custom(void **state) {
    TestState *ts = *state;
    // Arrange: load the fixture fully into memory up front
    int64_t size = 0;
    assert_int_equal(load_file(VIDEO_AUDIO_FILE, &ts->data, &size), 0);
    ts->mem = (MemFile){.data = ts->data, .size = size, .pos = 0};

    // Act
    ts->src = Kit_CreateSourceFromCustom(mem_read, mem_seek, &ts->mem);

    // Assert
    assert_non_null(ts->src);
    assert_int_equal(Kit_GetSourceStreamCount(ts->src), 2);
    // The fixture is 2 s; the container may report slightly over (muxer padding).
    assert_double_in_range(Kit_GetSourceDuration(ts->src), 1.5, 2.5);

    Kit_CloseSource(ts->src);
    ts->src = NULL;
    free(ts->data);
    ts->data = NULL;
}

/**
 * @brief Kit_GetSourceStreamInfo() rejects out-of-range indices (past-the-end and negative)
 * with the documented error return and a Kit_GetError() message, never an out-of-bounds read.
 */
static void test_stream_info_invalid_index(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_AUDIO_FILE);
    assert_non_null(ts->src);
    Kit_SourceStreamInfo info;

    // Act / Assert: far past the end
    Kit_ClearError();
    assert_int_equal(Kit_GetSourceStreamInfo(ts->src, &info, 999), 1);
    assert_non_null(Kit_GetError());

    // Act / Assert: negative index
    Kit_ClearError();
    assert_int_equal(Kit_GetSourceStreamInfo(ts->src, &info, -1), 1);
    assert_non_null(Kit_GetError());

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_GetBestSourceStream() finds streams by type, not position.
 * Regression guard against "stream 0 is always video": audio_first.mkv puts audio at 0.
 */
static void test_audio_first_stream_order(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(AUDIO_FIRST_FILE);
    assert_non_null(ts->src);

    // Act
    int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);

    // Assert: video stream exists but is not index 0
    assert_true(video_index > 0);

    Kit_SourceStreamInfo info;
    assert_int_equal(Kit_GetSourceStreamInfo(ts->src, &info, 0), 0);
    assert_int_equal(info.type, KIT_STREAMTYPE_AUDIO);

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_open_nonexistent_url_fails, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_open_null_url_fails, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_close_null_source_is_noop, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_source_duration_fractional, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_source_duration_unknown, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_stream_list, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_next_stream_iteration, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_source_from_io, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_source_from_custom, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_stream_info_invalid_index, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_audio_first_stream_order, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, kit_lifecycle_setup, kit_lifecycle_teardown);
}
