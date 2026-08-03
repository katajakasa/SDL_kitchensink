/**
 * End-to-end tests for Kit_Source stream enumeration and the Kit_Player
 * state machine, exercised against real container files under
 * KIT_TEST_DATA_DIR (one plain video+audio file, one with a subtitle track).
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
#include <string.h>

#include "kit_assert.h"
#include "kit_lifecycle.h"
#include "kit_playback.h"

#include <SDL.h>
#include <SDL_timer.h>

#include "kitchensink2/kitchensink.h"

#define VIDEO_FILE KIT_TEST_DATA_DIR "/video_audio.mp4"
#define VIDEO_ONLY_FILE KIT_TEST_DATA_DIR "/video_only.mp4"
#define AUDIO_ONLY_FILE KIT_TEST_DATA_DIR "/audio_only.m4a"
#define SUBTITLED_FILE KIT_TEST_DATA_DIR "/subtitled.mkv"

// SIZE_MAX as backend_buffer_size disables silence padding, per
// Kit_GetPlayerAudioData()'s doc comment.

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or let a failed test's live player threads
 * cascade into (and leak across) the remaining tests in the group. */
typedef struct {
    Kit_Source *src;
    Kit_Player *player;
    SDL_Surface *screen;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
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

/**
 * @brief A video+audio file reports exactly its two streams, resolves best streams, and reports a sane duration.
 */
static void test_source_stream_enumeration(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);

    // Act / Assert: stream counts, best-stream lookups, stream info, duration.
    assert_int_equal(Kit_GetSourceStreamCount(ts->src), 2);
    assert_true(Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO) >= 0);
    assert_true(Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO) >= 0);
    assert_int_equal(Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE), -1);

    Kit_SourceStreamInfo stream_info;
    assert_int_equal(Kit_GetSourceStreamInfo(ts->src, &stream_info, 0), 0);
    assert_true(stream_info.type == KIT_STREAMTYPE_VIDEO || stream_info.type == KIT_STREAMTYPE_AUDIO);

    const double duration = Kit_GetSourceDuration(ts->src);
    assert_double_in_range(duration, 1.0, 3.0);
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief A file muxed with a subtitle track reports a valid subtitle stream index.
 */
static void test_subtitle_stream_detected(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(SUBTITLED_FILE);
    assert_non_null(ts->src);

    // Act / Assert
    assert_true(Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE) >= 0);

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Player walks its full state machine (STOPPED -> PLAYING -> PAUSED -> STOPPED) plus a mid-walk seek.
 */
static void test_player_state_machine(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src,
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO),
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO),
        -1,
        NULL,
        NULL,
        160,
        120,
        NULL
    );
    assert_non_null(ts->player);

    // Act / Assert: walk the state machine, checking the state after each call.
    // A new player must start STOPPED and report the source's duration.
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_STOPPED);
    const double duration = Kit_GetPlayerDuration(ts->player);
    assert_double_in_range(duration, 1.0, 3.0);

    Kit_PlayerPlay(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_PLAYING);

    Kit_PlayerPause(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_PAUSED);

    assert_int_equal(Kit_PlayerSeek(ts->player, 1.0), 0);

    Kit_PlayerStop(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_STOPPED);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_GetPlayerAspectRatio() reports a positive numerator/denominator pair for a plain video+audio player.
 */
static void test_player_aspect_ratio(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src,
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO),
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO),
        -1,
        NULL,
        NULL,
        160,
        120,
        NULL
    );
    assert_non_null(ts->player);

    // Act / Assert
    int num = 0, den = 0;
    assert_int_equal(Kit_GetPlayerAspectRatio(ts->player, &num, &den), 0);
    assert_true(num > 0);
    assert_true(den > 0);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_PlayerSeek() on a stopped player starts playback from the seek target (KIT_PLAYING); the
 * clock rebases to the target once the first post-seek frame decodes.
 */
static void test_seek_starts_stopped_player(void **state) {
    TestState *ts = *state;
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src,
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO),
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO),
        -1,
        NULL,
        NULL,
        160,
        120,
        NULL
    );
    assert_non_null(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_STOPPED);

    // Act: seek the never-played player.
    assert_int_equal(Kit_PlayerSeek(ts->player, 0.5), 0);

    // Assert: playback started, and the clock rebases near the target (the rebase runs on the
    // decoder thread once the first frame lands, so poll bounded instead of asserting instantly).
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_PLAYING);
    double position = 0.0;
    for(int i = 0; i < 500 && position < 0.4; i++) {
        position = Kit_GetPlayerPosition(ts->player);
        if(position < 0.4)
            SDL_Delay(10);
    }
    assert_true(position >= 0.4);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Player reports back exactly the video/audio stream indices it was created with, and -1 for subtitle.
 */
static void test_player_stream_getters(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);

    ts->player = Kit_CreatePlayer(ts->src, video_index, audio_index, -1, NULL, NULL, 160, 120, NULL);
    assert_non_null(ts->player);

    // Act / Assert
    assert_int_equal(Kit_GetPlayerVideoStream(ts->player), video_index);
    assert_int_equal(Kit_GetPlayerAudioStream(ts->player), audio_index);
    assert_int_equal(Kit_GetPlayerSubtitleStream(ts->player), -1);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief An out-of-range video stream index is rejected with NULL + an error.
 * Also exercises the partial-build unwind path inside Kit_CreatePlayer() as an ASan leak check.
 */
static void test_create_player_invalid_stream_index(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    Kit_ClearError();

    // Act
    ts->player = Kit_CreatePlayer(ts->src, 99, -1, -1, NULL, NULL, 160, 120, NULL);

    // Assert
    assert_null(ts->player);
    assert_non_null(Kit_GetError());

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Requesting a subtitle stream without a video stream fails up front with a specific error message.
 */
static void test_create_player_subtitle_without_video(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    Kit_ClearError();

    // Act
    ts->player = Kit_CreatePlayer(ts->src, -1, -1, 0, NULL, NULL, 160, 120, NULL);

    // Assert
    assert_null(ts->player);
    assert_string_equal(Kit_GetError(), "Subtitle stream selected without video stream");

    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Passing -1 for all three stream indices succeeds and returns a valid, idle player with no decoders attached.
 * Not enforced by the code despite kitplayer.h implying a stream is required.
 */
static void test_create_player_no_streams(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    Kit_ClearError();

    // Act
    ts->player = Kit_CreatePlayer(ts->src, -1, -1, -1, NULL, NULL, 0, 0, NULL);

    // Assert
    assert_non_null(ts->player);
    assert_null(Kit_GetError());
    assert_int_equal(Kit_GetPlayerVideoStream(ts->player), -1);
    assert_int_equal(Kit_GetPlayerAudioStream(ts->player), -1);
    assert_int_equal(Kit_GetPlayerSubtitleStream(ts->player), -1);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_STOPPED);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_PlayerSeek() clamps out-of-range targets to [0, duration] instead of failing, and never changes player
 * state.
 */
static void test_seek_out_of_bounds(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src,
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO),
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO),
        -1,
        NULL,
        NULL,
        160,
        120,
        NULL
    );
    assert_non_null(ts->player);

    Kit_PlayerPlay(ts->player);
    const Kit_PlayerState state_before = Kit_GetPlayerState(ts->player);
    assert_int_equal(state_before, KIT_PLAYING);

    // Act / Assert: seek below 0 and past the end; both succeed, state untouched.
    assert_int_equal(Kit_PlayerSeek(ts->player, -5.0), 0);
    assert_int_equal(Kit_GetPlayerState(ts->player), state_before);

    const double duration = Kit_GetPlayerDuration(ts->player);
    assert_int_equal(Kit_PlayerSeek(ts->player, duration + 10.0), 0);
    assert_int_equal(Kit_GetPlayerState(ts->player), state_before);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_GetPlayerPosition() reads 0 before the clock is primed, advances monotonically (within jitter)
 * while playing, freezes on Pause, and rewinds to 0 on Stop.
 */
static void test_position_advances_when_playing(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src, -1, Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO), -1, NULL, NULL, 0, 0, NULL
    );
    assert_non_null(ts->player);

    // Act / Assert: pump audio and track the clock through play/pause/stop.
    unsigned char buffer[4096];
    Kit_PlayerPlay(ts->player);
    const double duration = Kit_GetPlayerDuration(ts->player);

    // An unprimed clock reads 0; pump audio until the first drained frame primes it.
    assert_true(Kit_GetPlayerPosition(ts->player) == 0.0);
    const Uint32 prime_start = SDL_GetTicks();
    while(Kit_GetPlayerPosition(ts->player) == 0.0 && SDL_GetTicks() - prime_start < WAIT_BOUND_MS) {
        Kit_GetPlayerAudioData(ts->player, 0, buffer, sizeof(buffer));
        SDL_Delay(5);
    }
    const double start_pos = Kit_GetPlayerPosition(ts->player);
    assert_true(start_pos > 0.0);
    assert_true(start_pos < duration);
    double last_pos = start_pos;

    // Negative tolerance: the clock can legitimately re-base backwards by a few ms when
    // resynchronizing, and a sanitizer-slowed scheduling stall can stretch that past 20 ms.
    // 100 ms still catches real regressions, which jump by whole seek/serial deltas.
    const double jitter_tolerance = 0.1;
    const Uint32 poll_start = SDL_GetTicks();
    while(SDL_GetTicks() - poll_start < 500) {
        Kit_GetPlayerAudioData(ts->player, 0, buffer, sizeof(buffer));
        const double pos = Kit_GetPlayerPosition(ts->player);
        assert_true(pos >= last_pos - jitter_tolerance);
        last_pos = pos;
        SDL_Delay(10);
    }
    assert_true(last_pos > start_pos);

    Kit_PlayerPause(ts->player);
    const double paused_pos_1 = Kit_GetPlayerPosition(ts->player);
    SDL_Delay(150);
    const double paused_pos_2 = Kit_GetPlayerPosition(ts->player);
    assert_true(paused_pos_1 == paused_pos_2);

    Kit_PlayerStop(ts->player);
    assert_true(Kit_GetPlayerPosition(ts->player) == 0.0);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_GetPlayerInfo() reports the negotiated video size, non-zero pixel/sample formats, and real codec names.
 */
static void test_player_info(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src,
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO),
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO),
        -1,
        NULL,
        NULL,
        160,
        120,
        NULL
    );
    assert_non_null(ts->player);

    // Act
    Kit_PlayerInfo info;
    Kit_GetPlayerInfo(ts->player, &info);

    // Assert
    assert_string_equal(info.video_codec.name, "h264");
    assert_true(strlen(info.audio_codec.name) > 0);

    assert_int_equal(info.video_format.width, 160);
    assert_int_equal(info.video_format.height, 120);
    assert_true(info.video_format.format != 0);
    assert_true(info.audio_format.format != 0);
    assert_true(info.audio_format.sample_rate > 0);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief A video-only player reports "no stream" values on every audio-side getter instead of crashing.
 */
static void test_video_only_player(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_ONLY_FILE);
    assert_non_null(ts->src);
    assert_int_equal(Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO), -1);

    ts->player = Kit_CreatePlayer(
        ts->src, Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO), -1, -1, NULL, NULL, 160, 120, NULL
    );
    assert_non_null(ts->player);

    // Act / Assert: audio side reports "no stream" everywhere.
    assert_int_equal(Kit_GetPlayerAudioStream(ts->player), -1);

    Kit_PlayerInfo info;
    Kit_GetPlayerInfo(ts->player, &info);
    assert_string_equal(info.audio_codec.name, "");

    unsigned char buffer[4096];
    Kit_PlayerPlay(ts->player);
    assert_int_equal(Kit_GetPlayerAudioData(ts->player, 0, buffer, sizeof(buffer)), 0);

    // Buffer-state getters signal stream presence in their return value.
    assert_false(Kit_GetPlayerAudioBufferState(ts->player, NULL, NULL, NULL, NULL));
    assert_true(Kit_GetPlayerVideoBufferState(ts->player, NULL, NULL, NULL, NULL));

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief An audio-only player reports "no stream" values on every video- and subtitle-side getter
 * instead of crashing, while audio still flows. Mirror of test_video_only_player.
 */
static void test_audio_only_player(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(AUDIO_ONLY_FILE);
    assert_non_null(ts->src);
    assert_int_equal(Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO), -1);

    ts->player = Kit_CreatePlayer(
        ts->src, -1, Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO), -1, NULL, NULL, 0, 0, NULL
    );
    assert_non_null(ts->player);

    // Act / Assert: video side reports "no stream" everywhere.
    assert_int_equal(Kit_GetPlayerVideoStream(ts->player), -1);
    assert_int_equal(Kit_GetPlayerSubtitleStream(ts->player), -1);

    Kit_PlayerInfo info;
    Kit_GetPlayerInfo(ts->player, &info);
    assert_string_equal(info.video_codec.name, "");

    Kit_PlayerPlay(ts->player);
    // No video decoder: the getter must return 1 ("no new frame") without touching the texture,
    // so a NULL texture is safe and proves it never dereferences the video path.
    assert_int_equal(Kit_GetPlayerVideoSDLTexture(ts->player, NULL, NULL), 1);

    unsigned char **items = NULL;
    SDL_Rect *sources = NULL;
    SDL_Rect *targets = NULL;
    assert_int_equal(Kit_GetPlayerSubtitleRawFrames(ts->player, &items, &sources, &targets), 0);

    // Buffer-state getters signal stream presence in their return value.
    assert_false(Kit_GetPlayerVideoBufferState(ts->player, NULL, NULL, NULL, NULL));
    assert_false(Kit_GetPlayerSubtitleBufferState(ts->player, NULL, NULL, NULL, NULL));
    assert_true(Kit_GetPlayerAudioBufferState(ts->player, NULL, NULL, NULL, NULL));

    // Audio itself must still work on this player.
    unsigned char buffer[4096];
    assert_true(wait_for_audio_data(ts->player, buffer, sizeof(buffer)) > 0);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_WaitBufferFillRate() with video percentages requested on an audio-only player
 * ignores the absent video buffers (zero capacity) and succeeds on the audio side alone.
 */
static void test_fill_rate_ignores_missing_video(void **state) {
    TestState *ts = *state;
    // Arrange: audio-only player from an A/V file (video deselected -> no video buffers).
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src, -1, Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO), -1, NULL, NULL, 0, 0, NULL
    );
    assert_non_null(ts->player);

    // Act: demand 50% from the nonexistent video buffers, and a known-reachable
    // 10% from the real audio output (the threshold other tests rely on; input
    // buffers drain too fast on a short file to hold any percentage).
    Kit_PlayerPlay(ts->player);
    const int ret = Kit_WaitBufferFillRate(ts->player, -1, 10, 50, 50, 5.0);

    // Assert: reached, not timed out -- the video percentages must be skipped.
    assert_int_equal(ret, 0);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_WaitBufferFillRate() with audio percentages requested on a video-only player
 * ignores the absent audio buffers (zero capacity) and succeeds on the video side alone.
 */
static void test_fill_rate_ignores_missing_audio(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_ONLY_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src, Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO), -1, -1, NULL, NULL, 160, 120, NULL
    );
    assert_non_null(ts->player);

    // Act: demand 50% from the nonexistent audio buffers, and a known-reachable
    // 50% from the real video output (the threshold other tests rely on).
    Kit_PlayerPlay(ts->player);
    const int ret = Kit_WaitBufferFillRate(ts->player, 50, 50, -1, 50, 5.0);

    // Assert
    assert_int_equal(ret, 0);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Every state-transition call is idempotent from a no-op state (Pause/Stop from STOPPED, repeated
 * Play/Pause/Stop). No sleeps or data pumping here: the fixture's lazy EOF-driven STOPPED flip must not fire mid-table
 *.
 */
static void test_state_transition_table(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src,
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO),
        Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO),
        -1,
        NULL,
        NULL,
        160,
        120,
        NULL
    );
    assert_non_null(ts->player);

    // Act / Assert: check the state after every transition call in the table.
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_STOPPED);

    Kit_PlayerPause(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_STOPPED);

    Kit_PlayerStop(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_STOPPED);

    Kit_PlayerPlay(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_PLAYING);

    Kit_PlayerPlay(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_PLAYING);

    Kit_PlayerPause(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_PAUSED);

    Kit_PlayerPause(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_PAUSED);

    Kit_PlayerPlay(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_PLAYING);

    Kit_PlayerStop(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_STOPPED);

    Kit_PlayerStop(ts->player);
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_STOPPED);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_GetPlayerAudioData() with length 0 returns 0 and never touches the destination buffer.
 */
static void test_audio_data_zero_length(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src, -1, Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO), -1, NULL, NULL, 0, 0, NULL
    );
    assert_non_null(ts->player);

    unsigned char buffer[16];
    memset(buffer, 0xAB, sizeof(buffer));

    // Act / Assert: a zero-length read returns 0 and leaves the buffer intact.
    Kit_PlayerPlay(ts->player);
    const int got = Kit_GetPlayerAudioData(ts->player, SIZE_MAX, buffer, 0);
    assert_int_equal(got, 0);

    unsigned char canary[16];
    memset(canary, 0xAB, sizeof(canary));
    assert_memory_equal(buffer, canary, sizeof(buffer));

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_GetPlayerAudioData() with a buffer size that is not a multiple of the sample-frame
 * size returns only whole frames -- never a partially copied sample.
 */
static void test_audio_data_odd_length(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src, -1, Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO), -1, NULL, NULL, 0, 0, NULL
    );
    assert_non_null(ts->player);

    Kit_PlayerInfo info;
    Kit_GetPlayerInfo(ts->player, &info);
    const int frame_bytes =
        (SDL_AUDIO_BITSIZE(info.audio_format.format) / 8) * Kit_GetChannelLayoutCount(info.audio_format.layout);
    assert_true(frame_bytes > 1); // an odd request must actually be able to split a frame

    // Act: request an odd number of bytes, guaranteed not a frame multiple.
    unsigned char buffer[4097];
    Kit_PlayerPlay(ts->player);
    const int received = wait_for_audio_data(ts->player, buffer, sizeof(buffer));

    // Assert: data arrived and is frame-aligned.
    assert_true(received > 0);
    assert_int_equal(received % frame_bytes, 0);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_GetPlayerSubtitleSDLTexture() with limit 0 returns 0 rects instead of overflowing zero-capacity output
 * arrays. The reason this whole group runs under KIT_INIT_ASS.
 */
static void test_subtitle_texture_zero_limit(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(SUBTITLED_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int subtitle_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE);
    assert_true(video_index >= 0);
    assert_true(subtitle_index >= 0);

    ts->player = Kit_CreatePlayer(ts->src, video_index, -1, subtitle_index, NULL, NULL, 160, 120, NULL);
    assert_non_null(ts->player);
    ts->screen = NULL;
    ts->renderer = NULL;
    create_headless_renderer(160, 120, &ts->screen, &ts->renderer);
    ts->texture = Kit_CreatePlayerSubtitleSDLTexture(ts->player, ts->renderer, 64, 64);
    assert_non_null(ts->texture);

    Kit_PlayerPlay(ts->player);

    // Act / Assert: limit 0 must yield 0 rects.
    SDL_Rect sources[4];
    SDL_Rect targets[4];
    const int got = Kit_GetPlayerSubtitleSDLTexture(ts->player, ts->texture, sources, targets, 0);
    assert_int_equal(got, 0);

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

/**
 * @brief Kit_LockPlayerVideoRawFrame() refuses while stopped, returns plane pointers and line
 * sizes consistent with the negotiated output format once playing, and
 * Kit_UnlockPlayerVideoRawFrame() releases the decoder ctrl lock so the next raw read can
 * proceed instead of deadlocking (a leaked lock is caught by the ctest timeout).
 */
static void test_video_raw_frame_lock_unlock(void **state) {
    TestState *ts = *state;
    // Arrange: video-only player, so undrained audio cannot stall the demuxer.
    ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
    assert_non_null(ts->src);
    ts->player = Kit_CreatePlayer(
        ts->src, Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO), -1, -1, NULL, NULL, 160, 120, NULL
    );
    assert_non_null(ts->player);

    unsigned char **data = NULL;
    int *line_size = NULL;
    SDL_Rect area;

    // A stopped player must refuse the raw lock.
    assert_int_equal(Kit_LockPlayerVideoRawFrame(ts->player, &data, &line_size, &area), 1);

    // Act: poll for the first raw frame, bounded by wall clock.
    Kit_PlayerPlay(ts->player);
    int ret = 1;
    const Uint32 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < WAIT_BOUND_MS && ret != 0) {
        ret = Kit_LockPlayerVideoRawFrame(ts->player, &data, &line_size, &area);
        if(ret != 0)
            SDL_Delay(10);
    }

    // Assert: the frame is coherent with the negotiated output size and format.
    assert_int_equal(ret, 0);
    assert_non_null(data);
    assert_non_null(line_size);
    assert_non_null(data[0]);
    assert_true(line_size[0] > 0);
    assert_int_equal(area.w, 160);
    assert_int_equal(area.h, 120);

    Kit_PlayerInfo info;
    Kit_GetPlayerInfo(ts->player, &info);
    if(info.video_format.format == SDL_PIXELFORMAT_YV12 || info.video_format.format == SDL_PIXELFORMAT_IYUV) {
        // YUV output splits into three planes (see Kit_LockPlayerVideoRawFrame() docs).
        assert_non_null(data[1]);
        assert_non_null(data[2]);
        assert_true(line_size[1] > 0);
        assert_true(line_size[2] > 0);
    }
    Kit_UnlockPlayerVideoRawFrame(ts->player);

    // A second lock attempt must return (with or without a new frame) rather than deadlock on a
    // ctrl lock the unlock above failed to release.
    ret = Kit_LockPlayerVideoRawFrame(ts->player, &data, &line_size, NULL);
    if(ret == 0)
        Kit_UnlockPlayerVideoRawFrame(ts->player);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/**
 * @brief Kit_ClosePlayer() immediately after Play(), with no decode progress, tears down cleanly.
 * Single-threaded; ASan/TSan are the actual checkers, looped 10x to hit a narrow shutdown race if one exists.
 */
static void test_close_immediately_after_play(void **state) {
    TestState *ts = *state;
    for(int i = 0; i < 10; i++) {
        // Arrange
        ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
        assert_non_null(ts->src);
        ts->player = Kit_CreatePlayer(
            ts->src,
            Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO),
            Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO),
            -1,
            NULL,
            NULL,
            160,
            120,
            NULL
        );
        assert_non_null(ts->player);

        // Act / Assert: the immediate teardown is checked by ASan/TSan.
        Kit_PlayerPlay(ts->player);
        Kit_ClosePlayer(ts->player);
        ts->player = NULL;
        Kit_CloseSource(ts->src);
        ts->src = NULL;
    }
}

/**
 * @brief 20x create->close without Play() exercises only the allocation/free paths, for cumulative ASan leak coverage.
 */
static void test_rapid_create_destroy(void **state) {
    TestState *ts = *state;
    for(int i = 0; i < 20; i++) {
        // Arrange
        ts->src = Kit_CreateSourceFromUrl(VIDEO_FILE);
        assert_non_null(ts->src);
        ts->player = Kit_CreatePlayer(
            ts->src,
            Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO),
            Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO),
            -1,
            NULL,
            NULL,
            160,
            120,
            NULL
        );
        assert_non_null(ts->player);

        // Act / Assert: the free paths are checked by ASan leak detection.
        Kit_ClosePlayer(ts->player);
        ts->player = NULL;
        Kit_CloseSource(ts->src);
        ts->src = NULL;
    }
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_source_stream_enumeration, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_subtitle_stream_detected, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_player_state_machine, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_player_aspect_ratio, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_seek_starts_stopped_player, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_player_stream_getters, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_player_invalid_stream_index, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_player_subtitle_without_video, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_player_no_streams, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_seek_out_of_bounds, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_position_advances_when_playing, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_player_info, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_video_only_player, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_audio_only_player, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_fill_rate_ignores_missing_video, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_fill_rate_ignores_missing_audio, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_state_transition_table, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_audio_data_zero_length, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_audio_data_odd_length, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_subtitle_texture_zero_limit, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_video_raw_frame_lock_unlock, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_close_immediately_after_play, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_rapid_create_destroy, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, kit_lifecycle_setup_video_ass, kit_lifecycle_teardown_video);
}
