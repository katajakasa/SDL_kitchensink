/**
 * Threaded unit tests for Kit_PacketBuffer (kitpacketbuffer.h): exercise the
 * *blocking* read/write paths from real SDL threads, since only real
 * concurrency lets TSan verify the mutex/condvar synchronization. Deliberately
 * duplicates the buffer/object setup from test_packetbuffer.c (that file is
 * single-threaded and stays under capacity; here blocking is the whole point).
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

#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include "kitchensink3/internal/kitpacketbuffer.h"

/** @brief Simple payload object standing in for AVPacket in buffer tests (mirrors test_packetbuffer.c's test_obj). */
typedef struct test_obj {
    int value;
} test_obj;

static void *obj_alloc(void) {
    return calloc(1, sizeof(test_obj));
}

static void obj_unref(void *obj) {
    ((test_obj *)obj)->value = 0;
}

static void obj_free(void **obj) {
    free(*obj);
    *obj = NULL;
}

static void obj_move(void *dst, void *src) {
    ((test_obj *)dst)->value = ((test_obj *)src)->value;
    ((test_obj *)src)->value = 0;
}

static void obj_ref(void *dst, void *src) {
    ((test_obj *)dst)->value = ((test_obj *)src)->value;
}

static Kit_PacketBuffer *create_buffer(size_t capacity) {
    return Kit_CreatePacketBuffer(capacity, obj_alloc, obj_unref, obj_free, obj_move, obj_ref);
}

#define FIFO_ITEM_COUNT 64
#define FIFO_BUFFER_CAPACITY 4  // smaller than FIFO_ITEM_COUNT, so the writer must block on a full buffer
#define FULL_WAIT_BOUND_MS 5000 // wall-clock bound for waiting on the buffer-full condition

typedef struct producer_ctx {
    Kit_PacketBuffer *buffer;
    int count;
} producer_ctx;

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or strand a live worker thread. The worker
 * context lives here too: heap-allocated state stays valid for a still-running worker even
 * after an assert longjmps out of the test body's frame. */
typedef struct {
    Kit_PacketBuffer *buffer;
    SDL_Thread *thread;
    producer_ctx ctx;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: unparks and joins any worker thread a mid-test assert failure left
 * behind (abort wakes both blocked readers and writers), then frees the buffer and the state,
 * so a failed test cannot strand a live thread or leak the buffer into the remaining tests. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    Kit_AbortPacketBuffer(ts->buffer); // NULL-safe; wakes any worker parked on the buffer
    if(ts->thread != NULL) {
        SDL_WaitThread(ts->thread, NULL);
        ts->thread = NULL;
    }
    Kit_FreePacketBuffer(&ts->buffer); // NULL-safe; NULLs the pointer
    free(ts);
    *state = NULL;
    return 0;
}

/**
 * @brief Writer thread body for test_producer_consumer_fifo: pushes `count` sequentially-valued packets, blocking as
 * needed.
 */
static int producer_thread(void *data) {
    producer_ctx *ctx = data;
    test_obj src;
    for(int i = 1; i <= ctx->count; i++) {
        src.value = i;
        if(!Kit_WritePacketBuffer(ctx->buffer, &src))
            return i; // failed write; report which item failed
    }
    return 0;
}

/**
 * @brief A writer thread pushing 64 packets through a capacity-4 buffer must preserve FIFO order across repeated
 * blocking stalls.
 */
static void test_producer_consumer_fifo(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->buffer = create_buffer(FIFO_BUFFER_CAPACITY);
    ts->ctx = (producer_ctx){.buffer = ts->buffer, .count = FIFO_ITEM_COUNT};
    test_obj dst;

    // Act: start the writer, then drain everything from the main thread
    ts->thread = SDL_CreateThread(producer_thread, "packetbuffer_mt_producer", &ts->ctx);
    assert_non_null(ts->thread);
    for(int i = 1; i <= FIFO_ITEM_COUNT; i++) {
        assert_true(Kit_ReadPacketBuffer(ts->buffer, &dst, 1000));
        assert_int_equal(dst.value, i); // FIFO order preserved across blocking stalls
    }

    // Assert: writer finished cleanly and the buffer is empty
    int writer_status = -1;
    SDL_WaitThread(ts->thread, &writer_status);
    ts->thread = NULL;
    assert_int_equal(writer_status, 0);
    assert_int_equal(Kit_GetPacketBufferLength(ts->buffer), 0);

    Kit_FreePacketBuffer(&ts->buffer);
}

/**
 * @brief Kit_AbortPacketBuffer() wakes a writer blocked on a full buffer, and its failed write is visible in the
 * thread exit code.
 */
static void test_abort_unblocks_writer(void **state) {
    TestState *ts = *state;
    // Arrange: fill the buffer to capacity so the next write blocks
    ts->buffer = create_buffer(FIFO_BUFFER_CAPACITY);
    ts->ctx = (producer_ctx){.buffer = ts->buffer, .count = FIFO_BUFFER_CAPACITY + 1};

    // Act: writer fills the buffer and then blocks on the (capacity+1)th write
    ts->thread = SDL_CreateThread(producer_thread, "packetbuffer_mt_writer", &ts->ctx);
    assert_non_null(ts->thread);
    // Wait on the observable condition (buffer full) instead of a fixed sleep:
    // once length == capacity, the writer is at (or just entering) the blocking
    // (capacity+1)th write, and the abort makes exactly that write fail whether
    // it has started blocking yet or not.
    const Uint32 wait_start = SDL_GetTicks();
    while(SDL_GetTicks() - wait_start < FULL_WAIT_BOUND_MS &&
          Kit_GetPacketBufferLength(ts->buffer) < FIFO_BUFFER_CAPACITY)
        SDL_Delay(1);
    assert_int_equal(Kit_GetPacketBufferLength(ts->buffer), FIFO_BUFFER_CAPACITY);
    Kit_AbortPacketBuffer(ts->buffer);

    // Assert: join returns promptly, and the blocked write is reported as failed
    int writer_status = 0;
    SDL_WaitThread(ts->thread, &writer_status);
    ts->thread = NULL;
    assert_int_equal(writer_status, FIFO_BUFFER_CAPACITY + 1); // the blocked write failed

    Kit_FreePacketBuffer(&ts->buffer);
}

/**
 * @brief Reader thread body for test_abort_unblocks_reader: blocks on Kit_BeginPacketBufferRead() against an empty
 * buffer.
 */
static int reader_thread(void *data) {
    Kit_PacketBuffer *buffer = data;
    test_obj dst;
    if(Kit_BeginPacketBufferRead(buffer, &dst, 5000))
        return 0; // unexpected: read succeeded on an empty, never-written buffer
    return 1;     // blocked read correctly failed once aborted
}

/**
 * @brief Kit_AbortPacketBuffer() wakes a reader blocked on an empty buffer well before its timeout elapses.
 */
static void test_abort_unblocks_reader(void **state) {
    TestState *ts = *state;
    // Arrange: empty buffer, nothing ever written to it
    ts->buffer = create_buffer(FIFO_BUFFER_CAPACITY);

    // Act: reader blocks on the empty buffer, then abort releases it
    ts->thread = SDL_CreateThread(reader_thread, "packetbuffer_mt_reader", ts->buffer);
    assert_non_null(ts->thread);
    SDL_Delay(50); // give the reader time to start blocking
    Kit_AbortPacketBuffer(ts->buffer);

    // Assert: join returns promptly (well under the 5s read timeout), and
    // the blocked read is reported as failed
    int reader_status = 0;
    SDL_WaitThread(ts->thread, &reader_status);
    ts->thread = NULL;
    assert_int_equal(reader_status, 1);

    Kit_FreePacketBuffer(&ts->buffer);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_producer_consumer_fifo, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_abort_unblocks_writer, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_abort_unblocks_reader, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
