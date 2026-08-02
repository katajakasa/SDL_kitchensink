/**
 * Lifecycle tests for Kit_DemuxerThread and Kit_DecoderThread: start,
 * liveness, packet flow, seeking, stop/wait/close, against video_audio.mp4.
 * CRITICAL: nothing here drains the packet buffers, and they are sized below
 * the fixture's packet count, so threads park on full-buffer writes:
 * Kit_AbortDemuxer()/Kit_AbortDecoder() must run between Stop and Wait/Close
 * or the join hangs forever, and Kit_SeekDemuxerThread() may only be called
 * while the demuxer thread is stopped and joined.
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

#include <SDL_timer.h>

#include "kit_lifecycle.h"

#include "kitchensink2/internal/kitdecoderthread.h"
#include "kitchensink2/internal/kitdemuxerthread.h"
#include "kitchensink2/internal/kittimer.h"
#include "kitchensink2/internal/video/kitvideo.h"
#include "kitchensink2/kitchensink.h"

#define VIDEO_FILE KIT_TEST_DATA_DIR "/video_audio.mp4"
#define POLL_ITERATIONS 200 // bounded poll guard (~2s at 10ms/iteration)
#define POLL_DELAY_MS 10

/** @brief Default player config for direct demuxer/decoder construction; filled in setup. */
static Kit_PlayerConfig g_config;

/** @brief Test lifecycle setup: reset the default config and bring the library up.
 * The video packet buffer is shrunk below the fixture's packet count (see file header) so the
 * demuxer thread provably parks on a full-buffer write instead of self-exiting at EOF. */
static int group_setup(void **state) {
    Kit_ResetPlayerConfig(&g_config);
    g_config.video.packet_buffer_size = 8;
    return kit_lifecycle_setup(state);
}

/**
 * @brief Polls Kit_GetPacketBufferLength(buffer) until it is >= min_length or POLL_ITERATIONS is exhausted.
 */
static bool wait_for_buffer_length(Kit_PacketBuffer *buffer, size_t min_length) {
    for(int i = 0; i < POLL_ITERATIONS; i++) {
        if(Kit_GetPacketBufferLength(buffer) >= min_length)
            return true;
        SDL_Delay(POLL_DELAY_MS);
    }
    return false;
}

/**
 * @brief Same as wait_for_buffer_length(), but for the timer's seek serial.
 */
static bool wait_for_serial(const Kit_Timer *timer, unsigned int min_serial) {
    for(int i = 0; i < POLL_ITERATIONS; i++) {
        if(Kit_GetTimerSerial(timer) >= min_serial)
            return true;
        SDL_Delay(POLL_DELAY_MS);
    }
    return false;
}

/**
 * @brief Same as wait_for_buffer_length(), but for a decoder's output buffer state.
 */
static bool wait_for_decoder_output(Kit_Decoder *decoder, unsigned int min_length) {
    unsigned int length = 0, capacity = 0;
    for(int i = 0; i < POLL_ITERATIONS; i++) {
        Kit_GetDecoderBufferState(decoder, &length, &capacity);
        if(length >= min_length)
            return true;
        SDL_Delay(POLL_DELAY_MS);
    }
    return false;
}

/**
 * @brief Bundles a source, demuxer, main timer, and (unstarted) demuxer thread over the
 * source's video stream -- the shared arrange/teardown of every test in this file.
 */
typedef struct demux_fixture {
    Kit_Source *src;
    int video_index;
    Kit_Demuxer *demuxer;
    Kit_Timer *timer;
    Kit_DemuxerThread *demux_thread;
} demux_fixture;

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or strand the library threads. The fixture
 * lives here too: the library threads keep touching it, so heap-allocated state stays valid
 * even after an assert longjmps out of the test body's frame. */
typedef struct {
    demux_fixture fx;
    Kit_Timer *video_timer;
    Kit_Decoder *decoder;
    Kit_DecoderThread *decoder_thread;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: replicates the tests' own shutdown sequence (see file header) for
 * whatever a mid-test assert failure left behind, then frees the state. Stop alone leaves
 * library threads parked on full-buffer writes forever, so the aborts must run before the
 * waits, and everything the threads use may only be freed after both joins. All calls are
 * NULL-safe no-ops. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    // (1) Signal stop, (2) unpark the threads (abort wakes full-buffer writers), (3) join.
    Kit_StopDecoderThread(ts->decoder_thread);
    Kit_StopDemuxerThread(ts->fx.demux_thread);
    Kit_AbortDecoder(ts->decoder);
    Kit_AbortDemuxer(ts->fx.demuxer);
    Kit_WaitDecoderThread(ts->decoder_thread);
    Kit_WaitDemuxerThread(ts->fx.demux_thread);
    // (4) Only now free what the threads used; Close* NULL the pointers themselves.
    Kit_CloseDecoderThread(&ts->decoder_thread);
    Kit_CloseDemuxerThread(&ts->fx.demux_thread);
    Kit_CloseDecoder(&ts->decoder);   // closes the video timer it owns
    Kit_CloseTimer(&ts->video_timer); // non-NULL only if never handed to a decoder
    Kit_CloseDemuxer(&ts->fx.demuxer);
    Kit_CloseTimer(&ts->fx.timer);
    Kit_CloseSource(ts->fx.src);
    ts->fx.src = NULL;
    free(ts);
    *state = NULL;
    return 0;
}

/** @brief Opens the video source and builds a demuxer plus an unstarted demuxer thread over its video stream. */
static void fixture_open(demux_fixture *fx) {
    fx->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(fx->src);
    fx->video_index = Kit_GetBestSourceStream(fx->src, KIT_STREAMTYPE_VIDEO);
    assert_true(fx->video_index >= 0);
    fx->demuxer = Kit_CreateDemuxer(fx->src, fx->video_index, -1, -1, &g_config);
    assert_non_null(fx->demuxer);
    fx->timer = Kit_CreateTimer();
    assert_non_null(fx->timer);
    fx->demux_thread = Kit_CreateDemuxerThread(fx->demuxer, fx->timer);
    assert_non_null(fx->demux_thread);
}

/** @brief Closes a demux_fixture's thread handle (no-op if a test already closed it), demuxer, timer, and source. */
static void fixture_close(demux_fixture *fx) {
    Kit_CloseDemuxerThread(&fx->demux_thread);
    Kit_CloseDemuxer(&fx->demuxer);
    Kit_CloseTimer(&fx->timer);
    Kit_CloseSource(fx->src);
    fx->src = NULL;
}

/**
 * @brief A demuxer thread starts, demuxes packets into its buffers, reports alive, and stops/waits/closes promptly.
 */
static void test_demuxer_thread_lifecycle(void **state) {
    TestState *ts = *state;
    // Arrange
    fixture_open(&ts->fx);

    // Act: start, and wait for packets to appear
    Kit_StartDemuxerThread(ts->fx.demux_thread);
    assert_true(Kit_IsDemuxerThreadAlive(ts->fx.demux_thread));

    Kit_PacketBuffer *video_buffer = Kit_GetDemuxerThreadPacketBuffer(ts->fx.demux_thread, KIT_VIDEO_INDEX);
    assert_non_null(video_buffer);
    assert_true(wait_for_buffer_length(video_buffer, 1));

    // Act: stop and join. Kit_AbortDemuxer() is required here -- see file
    // header -- because nothing has drained the (now full) video buffer.
    Kit_StopDemuxerThread(ts->fx.demux_thread);
    Kit_AbortDemuxer(ts->fx.demuxer);
    Kit_WaitDemuxerThread(ts->fx.demux_thread);

    // Assert: thread reports not alive, and close nulls the reference
    assert_false(Kit_IsDemuxerThreadAlive(ts->fx.demux_thread));
    Kit_CloseDemuxerThread(&ts->fx.demux_thread);
    assert_null(ts->fx.demux_thread);

    fixture_close(&ts->fx);
}

/**
 * @brief Creating a demuxer thread and closing it right away without starting it must not crash or leak.
 */
static void test_demuxer_thread_close_without_start(void **state) {
    TestState *ts = *state;
    // Arrange
    fixture_open(&ts->fx);

    // Act / Assert: close without ever starting
    Kit_CloseDemuxerThread(&ts->fx.demux_thread);
    assert_null(ts->fx.demux_thread);

    fixture_close(&ts->fx);
}

/**
 * @brief After stopping, seeking to the start, and restarting, the demuxer thread keeps producing packets.
 * Kit_SeekDemuxerThread() must only be called while the thread is stopped (see file header).
 */
static void test_seek_demuxer_thread(void **state) {
    TestState *ts = *state;
    // Arrange: get the thread running and producing packets
    fixture_open(&ts->fx);
    Kit_PacketBuffer *video_buffer = Kit_GetDemuxerThreadPacketBuffer(ts->fx.demux_thread, KIT_VIDEO_INDEX);
    assert_non_null(video_buffer);

    Kit_StartDemuxerThread(ts->fx.demux_thread);
    assert_true(wait_for_buffer_length(video_buffer, 1));

    // Act: stop (required before seeking; Kit_AbortDemuxer() needed first --
    // see file header), request a seek to 0, restart
    Kit_StopDemuxerThread(ts->fx.demux_thread);
    Kit_AbortDemuxer(ts->fx.demuxer);
    Kit_WaitDemuxerThread(ts->fx.demux_thread);
    const unsigned int serial_before = Kit_GetTimerSerial(ts->fx.timer);
    Kit_SeekDemuxerThread(ts->fx.demux_thread, 0);
    Kit_StartDemuxerThread(ts->fx.demux_thread);

    // Assert: thread is alive again, the seek actually ran (the serial bump in
    // Kit_DemuxerSeek() happens after the buffer flush, so everything buffered
    // past this point is post-seek), and fresh packets flow in after the seek
    // control packet
    assert_true(Kit_IsDemuxerThreadAlive(ts->fx.demux_thread));
    assert_true(wait_for_serial(ts->fx.timer, serial_before + 1));
    assert_true(wait_for_buffer_length(video_buffer, 2));

    Kit_StopDemuxerThread(ts->fx.demux_thread);
    Kit_AbortDemuxer(ts->fx.demuxer);
    Kit_WaitDemuxerThread(ts->fx.demux_thread);
    fixture_close(&ts->fx);
}

/**
 * @brief A decoder thread over a demuxer thread's video buffer fills its output buffer and stops/waits/closes promptly
 * once aborted. Must Stop then Kit_AbortDecoder() before Wait/Close (see file header), since nothing here drains the
 * output buffer.
 */
static void test_decoder_thread_lifecycle(void **state) {
    TestState *ts = *state;
    // Arrange: demuxer thread supplies video packets
    fixture_open(&ts->fx);
    Kit_PacketBuffer *video_input = Kit_GetDemuxerThreadPacketBuffer(ts->fx.demux_thread, KIT_VIDEO_INDEX);
    assert_non_null(video_input);

    // Arrange: real video decoder (default output buffer capacity)
    Kit_VideoFormatRequest request;
    Kit_ResetVideoFormatRequest(&request);
    ts->video_timer = Kit_CreateSecondaryTimer(ts->fx.timer, true);
    assert_non_null(ts->video_timer);
    // Kit_CreateVideoDecoder() takes ownership of the timer even on failure.
    ts->decoder = Kit_CreateVideoDecoder(
        ts->fx.src, &request, &g_config.video, g_config.thread_count, ts->video_timer, ts->fx.video_index
    );
    ts->video_timer = NULL;
    assert_non_null(ts->decoder);
    ts->decoder_thread = Kit_CreateDecoderThread(video_input, ts->decoder);
    assert_non_null(ts->decoder_thread);

    // Act: start both threads
    Kit_StartDemuxerThread(ts->fx.demux_thread);
    Kit_StartDecoderThread(ts->decoder_thread, "test video decoder thread");
    assert_true(Kit_IsDecoderThreadAlive(ts->decoder_thread));

    // Assert: output buffer actually fills to its (small, default) capacity,
    // since nothing here ever reads a frame back out
    assert_true(wait_for_decoder_output(ts->decoder, 2));
    // Give the decoder thread a moment to attempt (and block on) a 3rd
    // write; this isn't asserted directly (no observable state for
    // "blocked in a mutex wait"), it just makes the stop/abort sequence
    // below exercise the actual stall instead of a lucky race.
    SDL_Delay(50);

    // Act: the real unblock sequence (see file header) -- Stop alone would
    // leave the thread blocked in Kit_WritePacketBuffer(); Kit_AbortDecoder()
    // wakes it via Kit_AbortPacketBuffer() before Wait/Close try to join.
    Kit_StopDecoderThread(ts->decoder_thread);
    Kit_AbortDecoder(ts->decoder);
    Kit_WaitDecoderThread(ts->decoder_thread);

    // Assert: joined promptly (the CTest TIMEOUT is the deadlock backstop)
    assert_false(Kit_IsDecoderThreadAlive(ts->decoder_thread));
    Kit_CloseDecoderThread(&ts->decoder_thread);
    assert_null(ts->decoder_thread);

    // The demuxer thread may itself be stalled by now (the decoder thread
    // stopped draining its video packet buffer once it blocked above), so
    // it needs the same Kit_AbortDemuxer() treatment (see file header).
    Kit_StopDemuxerThread(ts->fx.demux_thread);
    Kit_AbortDemuxer(ts->fx.demuxer);
    Kit_WaitDemuxerThread(ts->fx.demux_thread);

    Kit_CloseDecoder(&ts->decoder);
    fixture_close(&ts->fx);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_demuxer_thread_lifecycle, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_demuxer_thread_close_without_start, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_seek_demuxer_thread, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_decoder_thread_lifecycle, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, group_setup, kit_lifecycle_teardown);
}
