/**
 * Unit tests for Kit_PacketBuffer (kitpacketbuffer.h), the generic thread-safe
 * FIFO ring buffer used to pass packets/frames between decoder threads.
 * WARNING for maintainers: Kit_WritePacketBuffer() on a buffer that is already
 * at capacity blocks forever (it waits for a reader to make room), so every
 * test here fills to at most `capacity` items and never past it.
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

#include "kitchensink3/internal/kitpacketbuffer.h"

/** @brief Simple payload object standing in for AVPacket in buffer tests. */
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

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or cascade into the remaining tests in the
 * group. Kit_FreePacketBuffer() NULLs the pointer itself, so each test's own free already
 * resets the member. */
typedef struct {
    Kit_PacketBuffer *buffer;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: releases whatever the TestState still holds, then the state
 * itself. Kit_FreePacketBuffer() is NULL-safe, so a clean pass leaves only the free. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    Kit_FreePacketBuffer(&ts->buffer);
    free(ts);
    *state = NULL;
    return 0;
}

/**
 * @brief A fresh buffer reports its capacity and zero length; Kit_FreePacketBuffer() nulls the
 * pointer and a second call on the now-NULL pointer is a safe no-op.
 */
static void test_create_and_free(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->buffer = create_buffer(4);

    // Act / Assert: creation state
    assert_non_null(ts->buffer);
    assert_int_equal(Kit_GetPacketBufferCapacity(ts->buffer), 4);
    assert_int_equal(Kit_GetPacketBufferLength(ts->buffer), 0);

    // Act / Assert: free nulls the pointer, and freeing again is a no-op
    Kit_FreePacketBuffer(&ts->buffer);
    assert_null(ts->buffer);
    Kit_FreePacketBuffer(&ts->buffer); // NULL-safe double free
}

/**
 * @brief The buffer is a FIFO: items come back out in the same order they were written.
 */
static void test_write_then_read_fifo(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->buffer = create_buffer(4);
    test_obj src, dst;

    // Act: write 1, 2, 3 in order
    for(int i = 1; i <= 3; i++) {
        src.value = i;
        assert_true(Kit_WritePacketBuffer(ts->buffer, &src));
    }
    assert_int_equal(Kit_GetPacketBufferLength(ts->buffer), 3);

    // Assert: read them back in the same order
    for(int i = 1; i <= 3; i++) {
        dst.value = -1;
        assert_true(Kit_ReadPacketBuffer(ts->buffer, &dst, 0));
        assert_int_equal(dst.value, i); // FIFO order
    }
    assert_int_equal(Kit_GetPacketBufferLength(ts->buffer), 0);
    Kit_FreePacketBuffer(&ts->buffer);
}

/**
 * @brief Reading from an empty buffer returns false rather than blocking, with both a zero and a non-zero timeout.
 */
static void test_read_empty_returns_false(void **state) {
    TestState *ts = *state;
    ts->buffer = create_buffer(2);
    test_obj dst;

    assert_false(Kit_ReadPacketBuffer(ts->buffer, &dst, 0));  // no timeout
    assert_false(Kit_ReadPacketBuffer(ts->buffer, &dst, 10)); // 10ms timeout

    Kit_FreePacketBuffer(&ts->buffer);
}

/**
 * @brief Filling a buffer exactly to its capacity succeeds and reports the correct length.
 * NOTE: writing to a FULL buffer blocks forever; only fill to capacity here, never past it.
 */
static void test_fill_to_capacity(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->buffer = create_buffer(4);
    test_obj src;

    // Act
    for(int i = 0; i < 4; i++) {
        src.value = i + 1;
        assert_true(Kit_WritePacketBuffer(ts->buffer, &src));
    }

    // Assert
    assert_int_equal(Kit_GetPacketBufferLength(ts->buffer), 4);
    Kit_FreePacketBuffer(&ts->buffer);
}

/**
 * @brief Kit_FlushPacketBuffer() drops all pending items, e.g. on seek.
 */
static void test_flush_empties_buffer(void **state) {
    TestState *ts = *state;
    ts->buffer = create_buffer(4);
    test_obj src = {42};
    assert_true(Kit_WritePacketBuffer(ts->buffer, &src));
    src.value = 43;
    assert_true(Kit_WritePacketBuffer(ts->buffer, &src));

    Kit_FlushPacketBuffer(ts->buffer);

    assert_int_equal(Kit_GetPacketBufferLength(ts->buffer), 0);
    Kit_FreePacketBuffer(&ts->buffer);
}

/**
 * @brief Once aborted, both reads and writes fail instead of blocking.
 */
static void test_abort_stops_io(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->buffer = create_buffer(4);
    test_obj src = {1}, dst;
    assert_true(Kit_WritePacketBuffer(ts->buffer, &src));

    // Act
    Kit_AbortPacketBuffer(ts->buffer);
    src.value = 2;

    // Assert
    assert_false(Kit_WritePacketBuffer(ts->buffer, &src));
    assert_false(Kit_ReadPacketBuffer(ts->buffer, &dst, 0));

    Kit_FreePacketBuffer(&ts->buffer);
}

/**
 * @brief Flushing an aborted buffer clears the aborted state so subsequent reads/writes work normally again.
 */
static void test_flush_clears_abort(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->buffer = create_buffer(4);
    test_obj src = {1}, dst;
    assert_true(Kit_WritePacketBuffer(ts->buffer, &src));
    Kit_AbortPacketBuffer(ts->buffer);

    // Act
    Kit_FlushPacketBuffer(ts->buffer);
    src.value = 3;

    // Assert
    assert_true(Kit_WritePacketBuffer(ts->buffer, &src));
    assert_true(Kit_ReadPacketBuffer(ts->buffer, &dst, 0));
    assert_int_equal(dst.value, 3);

    Kit_FreePacketBuffer(&ts->buffer);
}

/**
 * @brief Begin exposes the head item without removing it (a peek); Finish then consumes that peeked item for real.
 */
static void test_begin_finish_read(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->buffer = create_buffer(4);
    test_obj src = {7}, dst = {0};
    assert_true(Kit_WritePacketBuffer(ts->buffer, &src));

    // Act
    assert_true(Kit_BeginPacketBufferRead(ts->buffer, &dst, 0));
    assert_int_equal(dst.value, 7);
    Kit_FinishPacketBufferRead(ts->buffer);

    // Assert
    assert_int_equal(Kit_GetPacketBufferLength(ts->buffer), 0);

    Kit_FreePacketBuffer(&ts->buffer);
}

/**
 * @brief Cancel, unlike Finish, leaves the peeked item in the buffer so the next ordinary read can still pick it up.
 */
static void test_cancel_read_leaves_item(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->buffer = create_buffer(4);
    test_obj src = {8}, dst = {0};
    assert_true(Kit_WritePacketBuffer(ts->buffer, &src));

    // Act
    assert_true(Kit_BeginPacketBufferRead(ts->buffer, &dst, 0));
    Kit_CancelPacketBufferRead(ts->buffer);

    // Assert
    assert_int_equal(Kit_GetPacketBufferLength(ts->buffer), 1);
    assert_true(Kit_ReadPacketBuffer(ts->buffer, &dst, 0));
    assert_int_equal(dst.value, 8);

    Kit_FreePacketBuffer(&ts->buffer);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_create_and_free, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_write_then_read_fifo, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_read_empty_returns_false, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_fill_to_capacity, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_flush_empties_buffer, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_abort_stops_io, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_flush_clears_abort, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_begin_finish_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_cancel_read_leaves_item, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
