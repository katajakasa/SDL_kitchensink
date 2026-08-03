/**
 * Direct unit tests for Kit_Decoder (kitdecoder.h): codec info reporting,
 * feeding real demuxed packets and running the decoder, buffer clearing, and
 * resuming decode after a clear. Uses the real video decoder/demuxer against
 * video_audio.mp4, bypassing the decoder thread entirely.
 *
 * Kit_CreateVideoDecoder's output buffer defaults to only a few frames and
 * nothing here drains it, so Kit_WritePacketBuffer() would block forever once
 * full. setup() widens the buffer via the fixture's player config instead.
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

#include <libavcodec/avcodec.h>

#include "kit_lifecycle.h"

#include "kitchensink3/internal/kitdecoder.h"
#include "kitchensink3/internal/kitdemuxer.h"
#include "kitchensink3/internal/video/kitvideo.h"
#include "kitchensink3/kitchensink.h"

#define VIDEO_FILE KIT_TEST_DATA_DIR "/video_audio.mp4"
#define PUMP_LIMIT 200 // bounded demux/decode loop guard; well above what one packet needs

/**
 * @brief Player config for direct demuxer/decoder construction, with a widened video
 * output buffer (see file header); filled in setup.
 */
static Kit_PlayerConfig g_config;

/**
 * @brief Bundles a source, a demuxer over its video stream, and a video decoder
 * built straight from Kit_CreateVideoDecoder(), so each test can feed real
 * packets into the decoder without a decoder thread in the way.
 */
typedef struct decoder_fixture {
    Kit_Source *src;
    Kit_Demuxer *demuxer;
    Kit_Decoder *decoder;
    int video_index;
} decoder_fixture;

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them into the remaining tests. The fixture's
 * demuxer member always points at the live demuxer, even after test_flush_then_decode_again
 * swaps in a second one over src2. */
typedef struct {
    decoder_fixture fx;
    AVPacket *pkt;
    Kit_Source *src2;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: releases the fixture, scratch packet, and the second source
 * test_flush_then_decode_again opens, in the same order as the tests' own cleanup, then the
 * state itself, so a mid-test assert failure cannot leak them into the remaining tests. All
 * calls are NULL-safe no-ops and NULL the pointers. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    av_packet_free(&ts->pkt);
    Kit_CloseDecoder(&ts->fx.decoder);
    Kit_CloseDemuxer(&ts->fx.demuxer);
    Kit_CloseSource(ts->fx.src);
    ts->fx.src = NULL;
    Kit_CloseSource(ts->src2);
    ts->src2 = NULL;
    free(ts);
    *state = NULL;
    return 0;
}

/**
 * @brief Opens a video source, demuxer, and decoder for a test fixture.
 */
static void fixture_open(decoder_fixture *fx) {
    fx->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(fx->src);
    fx->video_index = Kit_GetBestSourceStream(fx->src, KIT_STREAMTYPE_VIDEO);
    assert_true(fx->video_index >= 0);
    fx->demuxer = Kit_CreateDemuxer(fx->src, fx->video_index, -1, -1, &g_config);
    assert_non_null(fx->demuxer);

    Kit_VideoFormatRequest request;
    Kit_ResetVideoFormatRequest(&request);
    Kit_Timer *timer = Kit_CreateTimer();
    assert_non_null(timer);
    // Kit_CreateVideoDecoder() takes ownership of the timer even on failure.
    fx->decoder =
        Kit_CreateVideoDecoder(fx->src, &request, &g_config.video, g_config.thread_count, timer, fx->video_index);
    assert_non_null(fx->decoder);
}

/**
 * @brief Closes a decoder_fixture's decoder, demuxer, and source.
 */
static void fixture_close(decoder_fixture *fx) {
    Kit_CloseDecoder(&fx->decoder);
    Kit_CloseDemuxer(&fx->demuxer);
    Kit_CloseSource(fx->src);
    fx->src = NULL;
}

/**
 * @brief Demuxes packets until one lands in the video buffer, then feeds it to the decoder, retrying on a queue-full
 * response. Mirrors the real decoder thread's retry loop (Kit_ProcessPacket() in src/internal/kitdecoderthread.c).
 * Returns false only on demuxer EOF.
 */
static bool pump_and_feed(Kit_Demuxer *demuxer, Kit_Decoder *decoder, AVPacket *pkt) {
    Kit_PacketBuffer *video_buffer = Kit_GetDemuxerPacketBuffer(demuxer, KIT_VIDEO_INDEX);
    for(int i = 0; i < PUMP_LIMIT; i++) {
        if(Kit_GetPacketBufferLength(video_buffer) > 0)
            break;
        if(!Kit_RunDemuxer(demuxer))
            return false;
    }
    if(!Kit_ReadPacketBuffer(video_buffer, pkt, 0))
        return false;

    Kit_DecoderInputResult ret = Kit_AddDecoderPacket(decoder, pkt);
    while(ret == KIT_DEC_INPUT_RETRY) {
        double pts;
        if(!Kit_RunDecoder(decoder, &pts))
            break; // nothing left to drain; give up on this packet rather than spin
        ret = Kit_AddDecoderPacket(decoder, pkt);
    }
    av_packet_unref(pkt);
    return true;
}

/**
 * @brief Feeds packets and runs the decoder until it reports a decoded frame, bounded by PUMP_LIMIT iterations.
 */
static void feed_until_decoded(decoder_fixture *fx, AVPacket *pkt, double *pts) {
    for(int i = 0; i < PUMP_LIMIT; i++) {
        if(!pump_and_feed(fx->demuxer, fx->decoder, pkt))
            break;
        if(Kit_RunDecoder(fx->decoder, pts))
            return;
    }
    fail_msg("decoder never produced a frame within %d demux/decode iterations", PUMP_LIMIT);
}

/**
 * @brief Widens the config's video output buffer (see file header) and initializes the library.
 */
static int group_setup(void **state) {
    Kit_ResetPlayerConfig(&g_config);
    g_config.video.frame_buffer_size = 8;
    return kit_lifecycle_setup(state);
}

/**
 * @brief Kit_GetDecoderCodecInfo() reports the real codec and Kit_GetDecoderStreamIndex() returns the decoder's stream
 * index.
 */
static void test_decoder_codec_info(void **state) {
    TestState *ts = *state;
    // Arrange
    fixture_open(&ts->fx);

    // Act
    Kit_Codec codec;
    int ret = Kit_GetDecoderCodecInfo(ts->fx.decoder, &codec);

    // Assert
    assert_int_equal(ret, 0);
    assert_string_equal(codec.name, "h264");
    assert_int_equal(Kit_GetDecoderStreamIndex(ts->fx.decoder), ts->fx.video_index);

    fixture_close(&ts->fx);
}

/**
 * @brief Feeding real demuxed packets via Kit_AddDecoderPacket() and running Kit_RunDecoder() eventually produces a
 * decoded frame.
 */
static void test_add_packet_and_run(void **state) {
    TestState *ts = *state;
    // Arrange
    fixture_open(&ts->fx);
    ts->pkt = av_packet_alloc();
    assert_non_null(ts->pkt);

    unsigned int length = 0, capacity = 0;
    assert_int_equal(Kit_GetDecoderBufferState(ts->fx.decoder, &length, &capacity), 0);
    assert_int_equal(length, 0);
    assert_true(capacity > 0);

    // Act
    double pts = -1.0;
    feed_until_decoded(&ts->fx, ts->pkt, &pts);

    // Assert
    assert_true(pts >= 0);
    assert_int_equal(Kit_GetDecoderBufferState(ts->fx.decoder, &length, &capacity), 0);
    assert_true(length > 0);

    av_packet_free(&ts->pkt);
    fixture_close(&ts->fx);
}

/**
 * @brief After decoding a frame, Kit_ClearDecoderBuffers() empties the decoder's output packet buffer.
 */
static void test_clear_decoder_buffers(void **state) {
    TestState *ts = *state;
    // Arrange: decode at least one frame first
    fixture_open(&ts->fx);
    ts->pkt = av_packet_alloc();
    assert_non_null(ts->pkt);
    double pts = -1.0;
    feed_until_decoded(&ts->fx, ts->pkt, &pts);

    unsigned int length = 0, capacity = 0;
    Kit_GetDecoderBufferState(ts->fx.decoder, &length, &capacity);
    assert_true(length > 0);

    // Act
    Kit_ClearDecoderBuffers(ts->fx.decoder);

    // Assert
    assert_int_equal(Kit_GetDecoderBufferState(ts->fx.decoder, &length, &capacity), 0);
    assert_int_equal(length, 0);

    av_packet_free(&ts->pkt);
    fixture_close(&ts->fx);
}

/**
 * @brief After Kit_ClearDecoderBuffers(), feeding fresh packets from a new demuxer pass over a second source lets
 * decoding resume. Uses a second, independently-opened source (rather than a new demuxer on the same source) since the
 * first source's read position has already passed the file's only keyframe, and avcodec_flush_buffers() needs a
 * keyframe to produce output again.
 */
static void test_flush_then_decode_again(void **state) {
    TestState *ts = *state;
    // Arrange: decode a frame, then clear
    fixture_open(&ts->fx);
    ts->pkt = av_packet_alloc();
    assert_non_null(ts->pkt);
    double pts = -1.0;
    feed_until_decoded(&ts->fx, ts->pkt, &pts);
    Kit_ClearDecoderBuffers(ts->fx.decoder);
    unsigned int length = 0, capacity = 0;
    assert_int_equal(Kit_GetDecoderBufferState(ts->fx.decoder, &length, &capacity), 0);
    assert_int_equal(length, 0);

    // Act: fresh demuxer pass, from a second source opened at the start of
    // the same file, feeding the same still-open decoder. The fixture's
    // demuxer member keeps pointing at the live demuxer across the swap.
    Kit_CloseDemuxer(&ts->fx.demuxer);
    ts->src2 = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src2);
    ts->fx.demuxer = Kit_CreateDemuxer(ts->src2, ts->fx.video_index, -1, -1, &g_config);
    assert_non_null(ts->fx.demuxer);
    pts = -1.0;
    feed_until_decoded(&ts->fx, ts->pkt, &pts);

    // Assert: decoding resumed and produced a real frame
    assert_true(pts >= 0);
    assert_int_equal(Kit_GetDecoderBufferState(ts->fx.decoder, &length, &capacity), 0);
    assert_true(length > 0);

    av_packet_free(&ts->pkt);
    Kit_CloseDemuxer(&ts->fx.demuxer);
    Kit_CloseSource(ts->src2);
    ts->src2 = NULL;
    Kit_CloseDecoder(&ts->fx.decoder);
    Kit_CloseSource(ts->fx.src);
    ts->fx.src = NULL;
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_decoder_codec_info, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_add_packet_and_run, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_clear_decoder_buffers, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_flush_then_decode_again, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, group_setup, kit_lifecycle_teardown);
}
