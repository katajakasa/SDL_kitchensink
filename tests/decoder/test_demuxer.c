/**
 * Test for Kit_Demuxer (kitdemuxer.h): reading packets off a real container
 * into the per-stream packet buffers, buffer-state reporting, and flushing.
 * WARNING for maintainers: writes to a full packet buffer block forever, so
 * this test stays well below the input packet buffers' default capacity and
 * never fills one.
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

#include "kit_lifecycle.h"

#include "kitchensink3/internal/kitdemuxer.h"
#include "kitchensink3/kitchensink.h"

#define VIDEO_FILE KIT_TEST_DATA_DIR "/video_audio.mp4"

/** @brief Default player config for direct Kit_CreateDemuxer() construction; filled in group setup. */
static Kit_PlayerConfig g_config;

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them across the remaining tests in the group. */
typedef struct {
    Kit_Source *src;
    Kit_Demuxer *demuxer;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: releases whatever the TestState still holds (demuxer before its
 * source), then the state itself. Tests NULL each member right after their own close, so only
 * what an assert-longjmp left behind is released here. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    Kit_CloseDemuxer(&ts->demuxer);
    Kit_CloseSource(ts->src);
    free(ts);
    *state = NULL;
    return 0;
}

/** @brief Test lifecycle setup: reset the default config and bring the library up. */
static int group_setup(void **state) {
    Kit_ResetPlayerConfig(&g_config);
    return kit_lifecycle_setup(state);
}

/**
 * @brief Demuxer routes packets into per-stream buffers, reports buffer state, and clears on demand.
 */
static void test_demuxer_reads_packets(void **state) {
    TestState *ts = *state;
    // Arrange: open a source with both video and audio streams
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    assert_true(video_index >= 0);
    assert_true(audio_index >= 0);
    ts->demuxer = Kit_CreateDemuxer(ts->src, video_index, audio_index, -1, &g_config);
    assert_non_null(ts->demuxer);

    // Act: demux a handful of packets (see file header for the capacity warning)
    for(int i = 0; i < 8; i++) {
        assert_true(Kit_RunDemuxer(ts->demuxer));
    }

    // Assert: packets landed in the per-stream buffers, and lengths add up
    const size_t video_len = Kit_GetPacketBufferLength(Kit_GetDemuxerPacketBuffer(ts->demuxer, KIT_VIDEO_INDEX));
    const size_t audio_len = Kit_GetPacketBufferLength(Kit_GetDemuxerPacketBuffer(ts->demuxer, KIT_AUDIO_INDEX));
    assert_int_equal(video_len + audio_len, 8);

    unsigned int length = 0, capacity = 0;
    Kit_GetDemuxerBufferState(ts->demuxer, KIT_VIDEO_INDEX, &length, &capacity);
    assert_true(capacity > 0);
    assert_int_equal(length, video_len);

    // Act / Assert: clearing must empty both stream buffers
    Kit_ClearDemuxerBuffers(ts->demuxer);
    assert_int_equal(Kit_GetPacketBufferLength(Kit_GetDemuxerPacketBuffer(ts->demuxer, KIT_VIDEO_INDEX)), 0);
    assert_int_equal(Kit_GetPacketBufferLength(Kit_GetDemuxerPacketBuffer(ts->demuxer, KIT_AUDIO_INDEX)), 0);

    Kit_CloseDemuxer(&ts->demuxer);
    assert_null(ts->demuxer);
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_demuxer_reads_packets, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, group_setup, kit_lifecycle_teardown);
}
