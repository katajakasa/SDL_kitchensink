/**
 * Player concurrency stress tests: hammer the public Kit_Player getter API
 * from worker threads while the main thread drives Play/Pause/Seek/Stop, so
 * TSan/ASan walk every lock acquisition under real concurrency. Probabilistic
 * stress tests; primary value is TSan/ASan coverage, not a pinned
 * interleaving; ~2s budget each (labeled `stress`, excludable with `ctest -LE stress`).
 * No cmocka asserts in worker threads -- longjmp from a non-test thread
 * corrupts the harness; workers only set atomics/counters, checked after
 * SDL_WaitThread(). Kit_GetPlayerState() may lazily flip EOF mid-test.
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

#include <SDL3/SDL.h>
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include "kitchensink3/kitchensink.h"

#define VIDEO_FILE KIT_TEST_DATA_DIR "/video_audio.mp4"
#define SUBTITLED_FULL_FILE KIT_TEST_DATA_DIR "/subtitled_full.mkv"

#define SCREEN_W 160
#define SCREEN_H 120
#define AUDIO_BUF_SIZE 4096
#define RECT_LIMIT 16

/**
 * @brief A generous per-worker iteration cap, on top of the SDL_AtomicInt stop flag:
 * a real bug (e.g. a getter deadlocking against a control-thread lock) must
 * not be able to hang these tests forever and blow the 120s CTest timeout in
 * a way that is hard to attribute. Chosen far larger than any of these
 * workers could plausibly reach within their ~1-2s budget.
 */
#define WORKER_ITER_CAP 2000000

typedef struct {
    Kit_Player *player;
    SDL_Texture *video_tex;
    SDL_AtomicInt stop;
    long iterations;
} RenderWorkerCtx;

typedef struct {
    Kit_Player *player;
    SDL_Texture *video_tex;
    SDL_AtomicInt *stop;
    long iterations;
} VideoWorkerCtx;

typedef struct {
    Kit_Player *player;
    SDL_AtomicInt *stop;
    long iterations;
} AudioWorkerCtx;

typedef struct {
    Kit_Player *player;
    SDL_Texture *sub_tex;
    SDL_AtomicInt *stop;
    long iterations;
} SubtitleWorkerCtx;

typedef struct {
    Kit_Player *player;
    SDL_Texture *video_tex;
    SDL_AtomicInt stop;
    long iterations;
} DrainWorkerCtx;

typedef struct {
    Kit_Player *player;
    SDL_AtomicInt stop;
    long iterations;
} StateWorkerCtx;

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or let a failed test's live worker/player
 * threads cascade into (and leak across) the remaining tests in the group. The worker contexts
 * (and the shared stop flag) live inside this heap block too: a stack-local ctx still
 * referenced by a running worker thread becomes use-after-scope after an assert-longjmp,
 * while this block stays alive until the teardown has joined every worker. */
typedef struct {
    Kit_Source *src;
    Kit_Player *player;
    SDL_Surface *screen;
    SDL_Renderer *renderer;
    SDL_Texture *video_tex;
    SDL_Texture *sub_tex;
    SDL_Thread *worker;
    SDL_Thread *video_worker;
    SDL_Thread *audio_worker;
    SDL_Thread *subtitle_worker;
    SDL_AtomicInt stop; // shared by the per-subsystem worker contexts below
    RenderWorkerCtx render_ctx;
    VideoWorkerCtx video_ctx;
    AudioWorkerCtx audio_ctx;
    SubtitleWorkerCtx subtitle_ctx;
    DrainWorkerCtx drain_ctx;
    StateWorkerCtx state_ctx;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: stops and joins any worker threads a mid-test assert failure left
 * running (workers first -- they poll the player about to be closed), then releases the player,
 * render target, and source, then the state itself, so a failed test's live threads cannot
 * cascade into (and leak across) the remaining tests in the group. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    SDL_SetAtomicInt(&ts->render_ctx.stop, 1);
    SDL_SetAtomicInt(&ts->stop, 1);
    SDL_SetAtomicInt(&ts->drain_ctx.stop, 1);
    SDL_SetAtomicInt(&ts->state_ctx.stop, 1);
    if(ts->worker != NULL)
        SDL_WaitThread(ts->worker, NULL);
    if(ts->video_worker != NULL)
        SDL_WaitThread(ts->video_worker, NULL);
    if(ts->audio_worker != NULL)
        SDL_WaitThread(ts->audio_worker, NULL);
    if(ts->subtitle_worker != NULL)
        SDL_WaitThread(ts->subtitle_worker, NULL);
    Kit_ClosePlayer(ts->player);
    if(ts->sub_tex != NULL)
        SDL_DestroyTexture(ts->sub_tex);
    if(ts->video_tex != NULL)
        SDL_DestroyTexture(ts->video_tex);
    if(ts->renderer != NULL)
        SDL_DestroyRenderer(ts->renderer);
    if(ts->screen != NULL)
        SDL_DestroySurface(ts->screen);
    Kit_CloseSource(ts->src);
    free(ts);
    *state = NULL;
    return 0;
}

/** @brief Worker: tight loop of video/audio/position getters until told to stop. */
static int render_worker_thread(void *data) {
    RenderWorkerCtx *ctx = data;
    unsigned char audio_buf[AUDIO_BUF_SIZE];
    SDL_Rect area;
    long i;
    for(i = 0; i < WORKER_ITER_CAP && !SDL_GetAtomicInt(&ctx->stop); i++) {
        Kit_GetPlayerVideoSDLTexture(ctx->player, ctx->video_tex, &area);
        Kit_GetPlayerAudioData(ctx->player, SIZE_MAX, audio_buf, sizeof(audio_buf));
        Kit_GetPlayerPosition(ctx->player);
    }
    ctx->iterations = i;
    return 0;
}

/**
 * @brief State transitions and getter calls run concurrently on separate threads without TSan races or ASan
 * use-after-free.
 */
static void test_render_thread_vs_control_thread(void **state) {
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
        SCREEN_W,
        SCREEN_H,
        NULL
    );
    assert_non_null(ts->player);

    create_headless_renderer(SCREEN_W, SCREEN_H, &ts->screen, &ts->renderer);
    ts->video_tex = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
    assert_non_null(ts->video_tex);

    ts->render_ctx = (RenderWorkerCtx){.player = ts->player, .video_tex = ts->video_tex, .iterations = 0};
    SDL_SetAtomicInt(&ts->render_ctx.stop, 0);

    ts->worker = SDL_CreateThread(render_worker_thread, "stress_render_worker", &ts->render_ctx);
    assert_non_null(ts->worker);

    // Act
    // 20 cycles of Play -> 20ms -> Pause -> Seek(0.5) -> Play -> Stop
    // (~20 * 20ms = 400ms of wall time, well inside the ~2s budget).
    for(int i = 0; i < 20; i++) {
        Kit_PlayerPlay(ts->player);
        SDL_Delay(20);
        Kit_PlayerPause(ts->player);
        Kit_PlayerSeek(ts->player, 0.5);
        Kit_PlayerPlay(ts->player);
        Kit_PlayerStop(ts->player);
    }

    SDL_SetAtomicInt(&ts->render_ctx.stop, 1);
    int worker_status = -1;
    SDL_WaitThread(ts->worker, &worker_status);
    ts->worker = NULL;

    // Assert
    assert_int_equal(worker_status, 0);
    assert_true(ts->render_ctx.iterations > 0);
    // The last control-thread call above was Kit_PlayerStop(), so the state
    // machine must have landed in KIT_STOPPED regardless of what the worker
    // was doing concurrently.
    assert_int_equal(Kit_GetPlayerState(ts->player), KIT_STOPPED);

    // The player must still be cleanly closable -- no corrupted internal
    // state left behind by the concurrent getters.
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    SDL_DestroyTexture(ts->video_tex);
    ts->video_tex = NULL;
    SDL_DestroyRenderer(ts->renderer);
    ts->renderer = NULL;
    SDL_DestroySurface(ts->screen);
    ts->screen = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/** @brief Worker: tight loop of Kit_GetPlayerVideoSDLTexture() until told to stop. */
static int video_getter_thread(void *data) {
    VideoWorkerCtx *ctx = data;
    long i;
    for(i = 0; i < WORKER_ITER_CAP && !SDL_GetAtomicInt(ctx->stop); i++) {
        Kit_GetPlayerVideoSDLTexture(ctx->player, ctx->video_tex, NULL);
    }
    ctx->iterations = i;
    return 0;
}

/** @brief Worker: tight loop of Kit_GetPlayerAudioData() until told to stop. */
static int audio_getter_thread(void *data) {
    AudioWorkerCtx *ctx = data;
    unsigned char audio_buf[AUDIO_BUF_SIZE];
    long i;
    for(i = 0; i < WORKER_ITER_CAP && !SDL_GetAtomicInt(ctx->stop); i++) {
        Kit_GetPlayerAudioData(ctx->player, SIZE_MAX, audio_buf, sizeof(audio_buf));
    }
    ctx->iterations = i;
    return 0;
}

/** @brief Worker: tight loop of Kit_GetPlayerSubtitleSDLTexture() until told to stop. */
static int subtitle_getter_thread(void *data) {
    SubtitleWorkerCtx *ctx = data;
    SDL_Rect sources[RECT_LIMIT];
    SDL_Rect targets[RECT_LIMIT];
    long i;
    for(i = 0; i < WORKER_ITER_CAP && !SDL_GetAtomicInt(ctx->stop); i++) {
        Kit_GetPlayerSubtitleSDLTexture(ctx->player, ctx->sub_tex, sources, targets, RECT_LIMIT);
    }
    ctx->iterations = i;
    return 0;
}

/**
 * @brief One getter thread per subsystem (video/audio/subtitle) runs concurrently against the same player without
 * corruption.
 */
static void test_concurrent_getters_per_subsystem(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->src = Kit_CreateSourceFromUrl(SUBTITLED_FULL_FILE);
    assert_non_null(ts->src);
    const int video_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_VIDEO);
    const int audio_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_AUDIO);
    const int subtitle_index = Kit_GetBestSourceStream(ts->src, KIT_STREAMTYPE_SUBTITLE);
    assert_true(video_index >= 0);
    assert_true(audio_index >= 0);
    assert_true(subtitle_index >= 0);

    ts->player =
        Kit_CreatePlayer(ts->src, video_index, audio_index, subtitle_index, NULL, NULL, SCREEN_W, SCREEN_H, NULL);
    assert_non_null(ts->player);

    create_headless_renderer(SCREEN_W, SCREEN_H, &ts->screen, &ts->renderer);
    ts->video_tex = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
    assert_non_null(ts->video_tex);
    ts->sub_tex = Kit_CreatePlayerSubtitleSDLTexture(ts->player, ts->renderer, 512, 512);
    assert_non_null(ts->sub_tex);

    SDL_SetAtomicInt(&ts->stop, 0);
    ts->video_ctx =
        (VideoWorkerCtx){.player = ts->player, .video_tex = ts->video_tex, .stop = &ts->stop, .iterations = 0};
    ts->audio_ctx = (AudioWorkerCtx){.player = ts->player, .stop = &ts->stop, .iterations = 0};
    ts->subtitle_ctx =
        (SubtitleWorkerCtx){.player = ts->player, .sub_tex = ts->sub_tex, .stop = &ts->stop, .iterations = 0};

    // Act
    Kit_PlayerPlay(ts->player);

    ts->video_worker = SDL_CreateThread(video_getter_thread, "stress_video_getter", &ts->video_ctx);
    ts->audio_worker = SDL_CreateThread(audio_getter_thread, "stress_audio_getter", &ts->audio_ctx);
    ts->subtitle_worker = SDL_CreateThread(subtitle_getter_thread, "stress_subtitle_getter", &ts->subtitle_ctx);
    assert_non_null(ts->video_worker);
    assert_non_null(ts->audio_worker);
    assert_non_null(ts->subtitle_worker);

    SDL_Delay(1000); // ~1s of concurrent playback, per the brief

    SDL_SetAtomicInt(&ts->stop, 1);
    int video_status = -1, audio_status = -1, subtitle_status = -1;
    SDL_WaitThread(ts->video_worker, &video_status);
    ts->video_worker = NULL;
    SDL_WaitThread(ts->audio_worker, &audio_status);
    ts->audio_worker = NULL;
    SDL_WaitThread(ts->subtitle_worker, &subtitle_status);
    ts->subtitle_worker = NULL;

    // Assert
    assert_int_equal(video_status, 0);
    assert_int_equal(audio_status, 0);
    assert_int_equal(subtitle_status, 0);
    assert_true(ts->video_ctx.iterations > 0);
    assert_true(ts->audio_ctx.iterations > 0);
    assert_true(ts->subtitle_ctx.iterations > 0);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    SDL_DestroyTexture(ts->sub_tex);
    ts->sub_tex = NULL;
    SDL_DestroyTexture(ts->video_tex);
    ts->video_tex = NULL;
    SDL_DestroyRenderer(ts->renderer);
    ts->renderer = NULL;
    SDL_DestroySurface(ts->screen);
    ts->screen = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/** @brief Worker: tight loop draining video+audio until told to stop. */
static int drain_worker_thread(void *data) {
    DrainWorkerCtx *ctx = data;
    unsigned char audio_buf[AUDIO_BUF_SIZE];
    long i;
    for(i = 0; i < WORKER_ITER_CAP && !SDL_GetAtomicInt(&ctx->stop); i++) {
        Kit_GetPlayerVideoSDLTexture(ctx->player, ctx->video_tex, NULL);
        Kit_GetPlayerAudioData(ctx->player, SIZE_MAX, audio_buf, sizeof(audio_buf));
    }
    ctx->iterations = i;
    return 0;
}

/**
 * @brief 50 rapid seeks against a draining worker leave the player still able to produce data afterward.
 * Seek return values during the storm are not asserted: EOF may flip state to KIT_STOPPED mid-storm.
 */
static void test_seek_storm(void **state) {
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
        SCREEN_W,
        SCREEN_H,
        NULL
    );
    assert_non_null(ts->player);

    create_headless_renderer(SCREEN_W, SCREEN_H, &ts->screen, &ts->renderer);
    ts->video_tex = Kit_CreatePlayerVideoSDLTexture(ts->player, ts->renderer, 0, 0);
    assert_non_null(ts->video_tex);

    Kit_PlayerPlay(ts->player);

    ts->drain_ctx = (DrainWorkerCtx){.player = ts->player, .video_tex = ts->video_tex, .iterations = 0};
    SDL_SetAtomicInt(&ts->drain_ctx.stop, 0);
    ts->worker = SDL_CreateThread(drain_worker_thread, "stress_seek_drain", &ts->drain_ctx);
    assert_non_null(ts->worker);

    // Act: the seek storm against the draining worker.
    for(int i = 0; i < 50; i++) {
        const double target = (i % 2 == 0) ? 0.2 : 1.5;
        Kit_PlayerSeek(ts->player, target); // return value intentionally ignored
    }

    // Act / Assert: the worker exited cleanly, and the player still produces data.
    SDL_SetAtomicInt(&ts->drain_ctx.stop, 1);
    int worker_status = -1;
    SDL_WaitThread(ts->worker, &worker_status);
    ts->worker = NULL;
    assert_int_equal(worker_status, 0);
    assert_true(ts->drain_ctx.iterations > 0);

    // The storm may have driven the player to EOF/KIT_STOPPED; restart if so.
    if(Kit_GetPlayerState(ts->player) == KIT_STOPPED) {
        Kit_PlayerPlay(ts->player);
    }
    assert_int_equal(Kit_PlayerSeek(ts->player, 0.2), 0);

    // Bounded poll (up to ~2s) for one successful data pull, proving the
    // player is still functional after the storm.
    unsigned char audio_buf[AUDIO_BUF_SIZE];
    bool got_data = false;
    for(int i = 0; i < 200 && !got_data; i++) {
        const int video_updated = Kit_GetPlayerVideoSDLTexture(ts->player, ts->video_tex, NULL) == 0;
        const int audio_read = Kit_GetPlayerAudioData(ts->player, SIZE_MAX, audio_buf, sizeof(audio_buf)) > 0;
        got_data = video_updated || audio_read;
        if(!got_data)
            SDL_Delay(10);
    }
    assert_true(got_data);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    SDL_DestroyTexture(ts->video_tex);
    ts->video_tex = NULL;
    SDL_DestroyRenderer(ts->renderer);
    ts->renderer = NULL;
    SDL_DestroySurface(ts->screen);
    ts->screen = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

/** @brief Worker: tight loop of state/buffer-state/duration getters until told to stop. */
static int state_query_thread(void *data) {
    StateWorkerCtx *ctx = data;
    unsigned int frames_length, frames_size, video_packets_length, video_packets_capacity;
    unsigned int samples_length, samples_size, audio_packets_length, audio_packets_capacity;
    long i;
    for(i = 0; i < WORKER_ITER_CAP && !SDL_GetAtomicInt(&ctx->stop); i++) {
        Kit_GetPlayerState(ctx->player);
        Kit_GetPlayerVideoBufferState(
            ctx->player, &frames_length, &frames_size, &video_packets_length, &video_packets_capacity
        );
        Kit_GetPlayerAudioBufferState(
            ctx->player, &samples_length, &samples_size, &audio_packets_length, &audio_packets_capacity
        );
        Kit_GetPlayerDuration(ctx->player);
    }
    ctx->iterations = i;
    return 0;
}

/**
 * @brief State/buffer-state/duration getters run concurrently with Play/Pause cycles without races or corruption.
 */
static void test_state_queries_during_playback(void **state) {
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
        SCREEN_W,
        SCREEN_H,
        NULL
    );
    assert_non_null(ts->player);

    ts->state_ctx = (StateWorkerCtx){.player = ts->player, .iterations = 0};
    SDL_SetAtomicInt(&ts->state_ctx.stop, 0);
    ts->worker = SDL_CreateThread(state_query_thread, "stress_state_query", &ts->state_ctx);
    assert_non_null(ts->worker);

    // Act
    // A handful of Play/Pause cycles over roughly a second of wall time.
    for(int i = 0; i < 10; i++) {
        Kit_PlayerPlay(ts->player);
        SDL_Delay(50);
        Kit_PlayerPause(ts->player);
        SDL_Delay(50);
    }

    // Act / Assert: join the worker and verify it ran to completion.
    SDL_SetAtomicInt(&ts->state_ctx.stop, 1);
    int worker_status = -1;
    SDL_WaitThread(ts->worker, &worker_status);
    ts->worker = NULL;
    assert_int_equal(worker_status, 0);
    assert_true(ts->state_ctx.iterations > 0);

    Kit_PlayerStop(ts->player);
    Kit_ClosePlayer(ts->player);
    ts->player = NULL;
    Kit_CloseSource(ts->src);
    ts->src = NULL;
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_render_thread_vs_control_thread, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_concurrent_getters_per_subsystem, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_seek_storm, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_state_queries_during_playback, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, kit_lifecycle_setup_video_ass, kit_lifecycle_teardown_video);
}
