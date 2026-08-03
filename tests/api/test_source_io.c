/**
 * Custom-IO fault-injection tests: a FaultyIO harness (in-memory buffer with
 * knobs to fail reads after N calls, fail every seek, or serve short reads)
 * exercising Kit_CreateSourceFromCustom()'s and Kit_Player's I/O error paths.
 * Mid-demux read failures and post-open seek failures are swallowed silently
 * by the library (no Kit_SetError, no non-zero return); the tests pin that.
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

#include "kit_lifecycle.h"
#include "kit_memsource.h"
#include "kit_playback.h"

#include <SDL.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>

#include "kitchensink3/kitchensink.h"

#define SCREEN_W 160
#define SCREEN_H 120

/**
 * @brief Fault-injecting custom IO source: serves a fixture file from memory and
 * can be told to fail reads after N calls, fail all seeks, or serve short
 * reads.
 */
typedef struct {
    unsigned char *data;
    size_t size;
    size_t pos;
    int fail_after_reads; // -1 = never
    int reads_done;
    // Atomic: test_seek_failure flips this from the main thread while the
    // player's demuxer thread is concurrently inside faulty_seek().
    SDL_atomic_t fail_seeks;
    int max_read; // -1 = unlimited; else short-read cap per call
} FaultyIO;

/**
 * @brief Kit_ReadCallback over a FaultyIO buffer; can fail after N reads or serve short reads.
 * reads_done is counted unconditionally so callers can measure a normal operation's read cost.
 */
static int faulty_read(void *userdata, uint8_t *buf, int size) {
    FaultyIO *io = userdata;
    io->reads_done++;
    if(io->fail_after_reads >= 0 && io->reads_done > io->fail_after_reads)
        return -1;
    if(io->pos >= io->size)
        return AVERROR_EOF; // matches src/kitsource.c's _RWReadCallback convention
    size_t left = io->size - io->pos;
    size_t want = (size_t)size;
    if(io->max_read > 0 && want > (size_t)io->max_read)
        want = io->max_read;
    if(want > left)
        want = left;
    memcpy(buf, io->data + io->pos, want);
    io->pos += want;
    return (int)want;
}

/** @brief Kit_SeekCallback over a FaultyIO buffer; can be told to fail every seek. */
static int64_t faulty_seek(void *userdata, int64_t offset, int whence) {
    FaultyIO *io = userdata;
    if(SDL_AtomicGet(&io->fail_seeks))
        return -1;
    if(whence & AVSEEK_SIZE)
        return (int64_t)io->size;
    int64_t new_pos;
    switch(whence & ~AVSEEK_FORCE) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (int64_t)io->pos + offset;
            break;
        case SEEK_END:
            new_pos = (int64_t)io->size + offset;
            break;
        default:
            return -1;
    }
    if(new_pos < 0 || new_pos > (int64_t)io->size)
        return -1;
    io->pos = (size_t)new_pos;
    return (int64_t)io->pos;
}

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or let a failed test's live player threads
 * cascade into (and leak across) the remaining tests in the group. The FaultyIO structs (and
 * data they point into) must live here (not stack-local): the player's demuxer thread keeps
 * calling faulty_read()/faulty_seek() against them until Kit_ClosePlayer() joins it, so after
 * an assert-longjmp a stack-local copy would be a dead frame under a still-running thread.
 * dummy backs io in test_immediate_eof. */
typedef struct {
    Kit_Source *src;
    Kit_Source *probe_src;
    Kit_Player *player;
    SDL_Surface *screen;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    unsigned char *data;
    unsigned char dummy;
    FaultyIO io;
    FaultyIO probe_io;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: releases whatever the TestState still holds (player first: that
 * joins the demuxer thread, after which freeing data -- still referenced by io -- is safe),
 * then the state itself. Tests NULL each member right after their own close, so only what an
 * assert-longjmp left behind is released here. */
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
    Kit_CloseSource(ts->probe_src);
    free(ts->data);
    free(ts);
    *state = NULL;
    return 0;
}

/**
 * @brief Reads capped to 3 bytes each must not break opening/enumerating a source.
 */
static void test_short_reads_ok(void **state) {
    TestState *ts = *state;
    // Arrange
    int64_t size = 0;
    assert_int_equal(load_file(VIDEO_AUDIO_FILE, &ts->data, &size), 0);
    ts->io = (FaultyIO){.data = ts->data, .size = (size_t)size, .fail_after_reads = -1, .max_read = 3};

    // Act
    ts->src = Kit_CreateSourceFromCustom(faulty_read, faulty_seek, &ts->io);

    // Assert
    assert_non_null(ts->src);
    assert_int_equal(Kit_GetSourceStreamCount(ts->src), 2);

    Kit_CloseSource(ts->src);
    ts->src = NULL;
    free(ts->data);
    ts->data = NULL;
}

/**
 * @brief A read failure on the very first call (before the header can be read) must
 * fail Kit_CreateSourceFromCustom() cleanly: NULL return plus a Kit_GetError() message.
 */
static void test_read_failure_at_open(void **state) {
    TestState *ts = *state;
    // Arrange
    int64_t size = 0;
    assert_int_equal(load_file(VIDEO_AUDIO_FILE, &ts->data, &size), 0);
    ts->io = (FaultyIO){.data = ts->data, .size = (size_t)size, .fail_after_reads = 0, .max_read = -1};
    Kit_ClearError();

    // Act
    ts->src = Kit_CreateSourceFromCustom(faulty_read, faulty_seek, &ts->io);

    // Assert
    assert_null(ts->src);
    assert_non_null(Kit_GetError());

    free(ts->data);
    ts->data = NULL;
}

/**
 * @brief A read failure partway through demuxing (simulating e.g. a dropped network
 * source) must not hang or crash; playback simply settles as if EOF had been reached.
 * No error is surfaced on this path -- this test asserts that silent
 * behavior: pump_av_until_idle() returns true and Kit_GetError() stays NULL.
 */
static void test_read_failure_mid_playback(void **state) {
    TestState *ts = *state;
    // Arrange: load the fixture, then measure how many reads a normal open
    // consumes so the real fault threshold can be placed well past it.
    int64_t size = 0;
    assert_int_equal(load_file(VIDEO_AUDIO_FILE, &ts->data, &size), 0);

    ts->probe_io = (FaultyIO){.data = ts->data, .size = (size_t)size, .fail_after_reads = -1, .max_read = 4096};
    ts->probe_src = Kit_CreateSourceFromCustom(faulty_read, faulty_seek, &ts->probe_io);
    assert_non_null(ts->probe_src);
    const int open_reads = ts->probe_io.reads_done;
    Kit_CloseSource(ts->probe_src);
    ts->probe_src = NULL;

    ts->io = (FaultyIO){.data = ts->data, .size = (size_t)size, .fail_after_reads = open_reads + 5, .max_read = 4096};
    Kit_ClearError();
    ts->src = Kit_CreateSourceFromCustom(faulty_read, faulty_seek, &ts->io);
    assert_non_null(ts->src);

    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    ts->player = Kit_CreatePlayer(ts->src, video_index, audio_index, -1, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    assert_non_null(ts->player);

    create_headless_renderer(SCREEN_W, SCREEN_H, &ts->screen, &ts->renderer);
    ts->texture = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
    assert_non_null(ts->texture);
    unsigned char audio_buffer[8192];

    // Act
    Kit_PlayerPlay(ts->player);
    const bool settled = pump_av_until_idle(ts->player, ts->texture, audio_buffer, sizeof(audio_buffer));

    // Assert: no hang, no surfaced error, player still closable cleanly.
    // Kit_GetError() is thread-local, so this only sees errors set by the
    // main-thread getters pumped above -- it deliberately pins that no getter
    // on the idle-pump path sets one today. When the error API rework starts
    // surfacing read failures (see file header), this assert must change too.
    assert_true(settled);
    assert_null(Kit_GetError());

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
    free(ts->data);
    ts->data = NULL;
}

/**
 * @brief A seek failure after playback has started must still report success from
 * Kit_PlayerSeek() (the real seek runs asynchronously on the demuxer thread and its
 * result is discarded); the player must stay in a consistent, usable state.
 */
static void test_seek_failure(void **state) {
    TestState *ts = *state;
    // Arrange
    int64_t size = 0;
    assert_int_equal(load_file(VIDEO_AUDIO_FILE, &ts->data, &size), 0);
    ts->io = (FaultyIO){.data = ts->data, .size = (size_t)size, .fail_after_reads = -1, .max_read = -1};
    ts->src = Kit_CreateSourceFromCustom(faulty_read, faulty_seek, &ts->io);
    assert_non_null(ts->src);

    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    ts->player = Kit_CreatePlayer(ts->src, video_index, audio_index, -1, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    assert_non_null(ts->player);
    Kit_PlayerPlay(ts->player);

    // Act: enable seek failure only now (post-open), then request a seek.
    SDL_AtomicSet(&ts->io.fail_seeks, 1);
    Kit_ClearError();
    const int ret = Kit_PlayerSeek(ts->player, 1.0);

    // Assert: documented actual behavior -- swallowed, reported as success.
    assert_int_equal(ret, 0);
    assert_null(Kit_GetError());

    // Player must still be in a consistent, queryable, drainable state.
    const Kit_PlayerState post_seek_state = Kit_GetPlayerState(ts->player);
    assert_true(post_seek_state == KIT_PLAYING || post_seek_state == KIT_STOPPED);

    create_headless_renderer(SCREEN_W, SCREEN_H, &ts->screen, &ts->renderer);
    ts->texture = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
    assert_non_null(ts->texture);
    unsigned char audio_buffer[8192];
    // One bounded pull of each kind -- not asserting data arrives (state may
    // already be draining/stopped), just that the calls remain well-behaved.
    Kit_GetPlayerAudioData(ts->player, SIZE_MAX, audio_buffer, sizeof(audio_buffer));
    Kit_GetPlayerVideoSDLTexture(ts->player, ts->texture, NULL);

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
    free(ts->data);
    ts->data = NULL;
}

/**
 * @brief A zero-size buffer (immediate EOF) must fail Kit_CreateSourceFromCustom()
 * cleanly, same contract as test_read_failure_at_open.
 */
static void test_immediate_eof(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->io = (FaultyIO){.data = &ts->dummy, .size = 0, .fail_after_reads = -1, .max_read = -1};
    Kit_ClearError();

    // Act
    ts->src = Kit_CreateSourceFromCustom(faulty_read, faulty_seek, &ts->io);

    // Assert
    assert_null(ts->src);
    assert_non_null(Kit_GetError());
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_short_reads_ok, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_read_failure_at_open, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_read_failure_mid_playback, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_seek_failure, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_immediate_eof, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, kit_lifecycle_setup_video, kit_lifecycle_teardown_video);
}
