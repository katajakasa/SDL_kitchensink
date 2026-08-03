/**
 * Stream-switching tests for Kit_SetPlayerStream()/Kit_GetPlayerStream()/
 * Kit_ClosePlayerStream() (src/kitplayer.c). A failed switch leaves the old
 * stream's playback untouched (new decoder built before the old is torn
 * down); a switch after demuxer EOF re-sends the EOF sentinel so the new
 * decoder winds down cleanly -- resuming playback on the new track still
 * requires a seek.
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
#include "kit_playback.h"

#include <SDL.h>
#include <SDL_timer.h>

#include "kitchensink3/kitchensink.h"

#define DUAL_AUDIO_FILE KIT_TEST_DATA_DIR "/dual_audio.mkv"
#define SUBTITLED_FILE KIT_TEST_DATA_DIR "/subtitled.mkv"

#define SCREEN_W 160
#define SCREEN_H 120
#define STREAM_LIST_LIMIT 8

/**
 * @brief Bounded pump budget: generous relative to the 2s fixtures, so a genuine
 * stall still fails reasonably fast while the per-test CTest TIMEOUT (120s)
 * remains the real backstop.
 */
#define PUMP_ITERS 300
#define PUMP_DELAY_MS 10

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or let a failed test's live player threads
 * cascade into (and leak across) the remaining tests in the group. */
typedef struct {
    Kit_Source *src;
    Kit_Player *player;
    SDL_Surface *screen;
    SDL_Renderer *renderer;
    SDL_Texture *video_tex;
    SDL_Texture *sub_tex;
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
    if(ts->sub_tex != NULL)
        SDL_DestroyTexture(ts->sub_tex);
    if(ts->video_tex != NULL)
        SDL_DestroyTexture(ts->video_tex);
    if(ts->renderer != NULL)
        SDL_DestroyRenderer(ts->renderer);
    if(ts->screen != NULL)
        SDL_FreeSurface(ts->screen);
    Kit_CloseSource(ts->src);
    free(ts);
    *state = NULL;
    return 0;
}

// -- test_get_player_stream --------------------------------------------

/**
 * @brief Kit_GetPlayerStream() echoes back exactly the stream indices a player was created with, for all three stream
 * types.
 */
static void test_get_player_stream(void **state) {
    TestState *ts = *state;

    // Arrange / Act / Assert: video + audio, dual_audio.mkv. The pair is fully closed
    // before the video + subtitle pair below is opened, so the one state pair is reused.
    ts->src = Kit_CreateSourceFromUrl(DUAL_AUDIO_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    assert_true(video_index >= 0);
    assert_true(audio_index >= 0);
    ts->player = Kit_CreatePlayer(ts->src, video_index, audio_index, -1, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    assert_non_null(ts->player);

    assert_int_equal(Kit_GetPlayerStream(ts->player, KIT_STREAMTYPE_VIDEO), video_index);
    assert_int_equal(Kit_GetPlayerStream(ts->player, KIT_STREAMTYPE_AUDIO), audio_index);
    assert_int_equal(Kit_GetPlayerStream(ts->player, KIT_STREAMTYPE_SUBTITLE), -1);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;

    // Arrange / Act / Assert: video + subtitle, subtitled.mkv.
    ts->src = Kit_CreateSourceFromUrl(SUBTITLED_FILE);
    assert_non_null(ts->src);
    const int vs_video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int subtitle_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE);
    assert_true(vs_video_index >= 0);
    assert_true(subtitle_index >= 0);
    ts->player = Kit_CreatePlayer(ts->src, vs_video_index, -1, subtitle_index, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    assert_non_null(ts->player);

    assert_int_equal(Kit_GetPlayerStream(ts->player, KIT_STREAMTYPE_VIDEO), vs_video_index);
    assert_int_equal(Kit_GetPlayerStream(ts->player, KIT_STREAMTYPE_AUDIO), -1);
    assert_int_equal(Kit_GetPlayerStream(ts->player, KIT_STREAMTYPE_SUBTITLE), subtitle_index);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_switch_audio_track -------------------------------------------

/**
 * @brief Switching audio tracks mid-play succeeds, reports the new index immediately, and renegotiates channel layout.
 * The short fixture is typically at EOF by switch time, so a seek follows the switch to resume playback.
 */
static void test_switch_audio_track(void **state) {
    TestState *ts = *state;

    // Arrange. No video stream is selected here: the demuxer only buffers
    // packets for tracked stream slots (Kit_RunDemuxer(), kitdemuxer.c), so
    // leaving video untracked avoids its packet buffer filling up and
    // stalling the shared demuxer thread while this test only drains audio.
    ts->src = Kit_CreateSourceFromUrl(DUAL_AUDIO_FILE);
    assert_non_null(ts->src);
    int audio_list[STREAM_LIST_LIMIT] = {0};
    const int audio_count = Kit_GetSourceStreamList(ts->src, KIT_STREAMTYPE_AUDIO, audio_list, STREAM_LIST_LIMIT);
    assert_int_equal(audio_count, 2);
    const int track_a = audio_list[0]; // stereo, 440 Hz
    const int track_b = audio_list[1]; // mono, 880 Hz

    ts->player = Kit_CreatePlayer(ts->src, -1, track_a, -1, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    assert_non_null(ts->player);
    Kit_PlayerPlay(ts->player);

    Kit_PlayerInfo info_before;
    Kit_GetPlayerInfo(ts->player, &info_before);
    assert_int_equal(Kit_GetChannelLayoutCount(info_before.audio_format.layout), 2);

    // Act: prime audio flow on track A, then switch.
    assert_true(pump_until_audio_flows(ts->player));
    assert_int_equal(Kit_SetPlayerStream(ts->player, KIT_STREAMTYPE_AUDIO, track_b), 0);

    // Assert: getter reports the new index immediately.
    assert_int_equal(Kit_GetPlayerStream(ts->player, KIT_STREAMTYPE_AUDIO), track_b);

    // The fixture has usually hit EOF by now, so seek back to where playback already
    // was to restart the demuxer and get data flowing on the new track.
    assert_int_equal(Kit_PlayerSeek(ts->player, Kit_GetPlayerPosition(ts->player)), 0);

    // Assert: audio data flows for the newly-selected track.
    assert_true(pump_until_audio_flows(ts->player));

    // Assert: reported channel count changed to match the new (mono) track.
    Kit_PlayerInfo info_after;
    Kit_GetPlayerInfo(ts->player, &info_after);
    assert_int_equal(Kit_GetChannelLayoutCount(info_after.audio_format.layout), 1);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_switch_after_eof ---------------------------------------------

/**
 * @brief Switching audio tracks after the demuxer thread has exited at EOF leaves the player able to
 * wind down to KIT_STOPPED (the new decoder receives a re-sent EOF sentinel) instead of hanging
 * forever on a starved packet buffer.
 */
static void test_switch_after_eof(void **state) {
    TestState *ts = *state;

    // Arrange: audio-only player on track A, played until the whole stream is drained.
    // Kit_GetPlayerState() is deliberately not polled before the switch: the lazy EOF
    // state transition would flip the player to KIT_STOPPED and skip
    // starting the new decoder thread, dodging the starved-buffer path pinned here.
    ts->src = Kit_CreateSourceFromUrl(DUAL_AUDIO_FILE);
    assert_non_null(ts->src);
    int audio_list[STREAM_LIST_LIMIT] = {0};
    const int audio_count = Kit_GetSourceStreamList(ts->src, KIT_STREAMTYPE_AUDIO, audio_list, STREAM_LIST_LIMIT);
    assert_int_equal(audio_count, 2);

    ts->player = Kit_CreatePlayer(ts->src, -1, audio_list[0], -1, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    assert_non_null(ts->player);
    Kit_PlayerPlay(ts->player);
    assert_true(pump_until_audio_flows(ts->player));
    assert_true(drain_audio_to_eof(ts->player));

    // Act: switch to track B now that the demuxer thread is (all but certainly) gone.
    assert_int_equal(Kit_SetPlayerStream(ts->player, KIT_STREAMTYPE_AUDIO, audio_list[1]), 0);
    assert_int_equal(Kit_GetPlayerStream(ts->player, KIT_STREAMTYPE_AUDIO), audio_list[1]);

    // Assert: the player reaches KIT_STOPPED within the bounded budget. Pre-fix, the new
    // decoder thread waits forever on a buffer nothing will ever fill, and the state
    // stays KIT_PLAYING until this loop runs out.
    Kit_PlayerState player_state = KIT_PLAYING;
    for(int i = 0; i < PUMP_ITERS && player_state != KIT_STOPPED; i++) {
        player_state = Kit_GetPlayerState(ts->player);
        if(player_state != KIT_STOPPED)
            SDL_Delay(PUMP_DELAY_MS);
    }
    assert_int_equal(player_state, KIT_STOPPED);

    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_switch_to_invalid_track --------------------------------------

/**
 * @brief Switching to a nonexistent index or a wrong-type stream index fails cleanly, leaving the current stream
 * untouched.
 */
static void test_switch_to_invalid_track(void **state) {
    TestState *ts = *state;

    // Arrange. As in test_switch_audio_track, no video stream is selected
    // for the player so the shared demuxer thread isn't stalled by an
    // undrained video packet buffer -- but the video stream's index is
    // still fetched from the source, to feed it into Kit_SetPlayerStream()
    // as a wrong-type audio index below.
    ts->src = Kit_CreateSourceFromUrl(DUAL_AUDIO_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    assert_true(video_index >= 0);
    assert_true(audio_index >= 0);

    ts->player = Kit_CreatePlayer(ts->src, -1, audio_index, -1, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    assert_non_null(ts->player);
    Kit_PlayerPlay(ts->player);
    assert_true(pump_until_audio_flows(ts->player));

    // Act / Assert: out-of-bounds index.
    Kit_ClearError();
    assert_int_equal(Kit_SetPlayerStream(ts->player, KIT_STREAMTYPE_AUDIO, 99), 1);
    assert_non_null(Kit_GetError());
    assert_int_equal(Kit_GetPlayerStream(ts->player, KIT_STREAMTYPE_AUDIO), audio_index);
    assert_true(pump_until_audio_flows(ts->player));

    // Act / Assert: a video stream index fed in as the audio type.
    Kit_ClearError();
    assert_int_equal(Kit_SetPlayerStream(ts->player, KIT_STREAMTYPE_AUDIO, video_index), 1);
    assert_non_null(Kit_GetError());
    assert_int_equal(Kit_GetPlayerStream(ts->player, KIT_STREAMTYPE_AUDIO), audio_index);
    assert_true(pump_until_audio_flows(ts->player));

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

// -- test_close_subtitle_stream_mid_play -------------------------------

/**
 * @brief Closing the subtitle stream mid-play succeeds; subtitle getters then report "no stream" and video keeps
 * playing.
 */
static void test_close_subtitle_stream_mid_play(void **state) {
    TestState *ts = *state;

    // Arrange
    ts->src = Kit_CreateSourceFromUrl(SUBTITLED_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int subtitle_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE);
    assert_true(video_index >= 0);
    assert_true(subtitle_index >= 0);

    ts->player = Kit_CreatePlayer(ts->src, video_index, -1, subtitle_index, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    assert_non_null(ts->player);

    create_headless_renderer(SCREEN_W, SCREEN_H, &ts->screen, &ts->renderer);
    ts->video_tex = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
    assert_non_null(ts->video_tex);
    ts->sub_tex = Kit_CreatePlayerSubtitleSDLTexture(ts->player, ts->renderer, 512, 512);
    assert_non_null(ts->sub_tex);

    Kit_PlayerPlay(ts->player);

    // Act: pump until subtitle rects appear, confirming subtitles are
    // actually active before closing the stream.
    SDL_Rect sources[16];
    SDL_Rect targets[16];
    const int got = pump_until_subtitle_rects(ts->player, ts->video_tex, ts->sub_tex, sources, targets, 16);
    assert_true(got > 0);

    // Act: close the subtitle stream.
    assert_int_equal(Kit_ClosePlayerStream(ts->player, KIT_STREAMTYPE_SUBTITLE), 0);

    // Assert: subtitle getters now report no stream selected / no data.
    assert_int_equal(Kit_GetPlayerStream(ts->player, KIT_STREAMTYPE_SUBTITLE), -1);
    assert_int_equal(Kit_GetPlayerSubtitleStream(ts->player), -1);
    assert_int_equal(Kit_GetPlayerSubtitleSDLTexture(ts->player, ts->sub_tex, sources, targets, 16), 0);

    // Assert: video keeps flowing after the subtitle stream is closed.
    bool video_flowed = false;
    for(int i = 0; i < PUMP_ITERS && !video_flowed; i++) {
        if(Kit_GetPlayerVideoSDLTexture(ts->player, ts->video_tex, NULL) == 0)
            video_flowed = true;
        else
            SDL_Delay(PUMP_DELAY_MS);
    }
    assert_true(video_flowed);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    SDL_DestroyTexture(ts->sub_tex);
    ts->sub_tex = NULL;
    SDL_DestroyTexture(ts->video_tex);
    ts->video_tex = NULL;
    SDL_DestroyRenderer(ts->renderer);
    ts->renderer = NULL;
    SDL_FreeSurface(ts->screen);
    ts->screen = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_get_player_stream, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_switch_audio_track, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_switch_after_eof, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_switch_to_invalid_track, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_close_subtitle_stream_mid_play, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, kit_lifecycle_setup_video_ass, kit_lifecycle_teardown_video);
}
