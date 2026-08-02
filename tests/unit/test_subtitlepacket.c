/**
 * Unit tests for Kit_SubtitlePacket (kitsubtitlepacket.h/.c), the value
 * object carrying a rendered subtitle surface plus its timing/position.
 * No Kit_Init()/SDL_Init() needed. ASan is what validates the ref-move/
 * ref-del tests -- a leak or double-free surfaces as an ASan failure, not
 * a wrong return value.
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

#include "kitchensink2/internal/subtitle/kitsubtitlepacket.h"

/** @brief Allocates a small 4x4 RGBA surface to act as a caller-owned subtitle bitmap. */
static SDL_Surface *create_test_surface(void) {
    return SDL_CreateRGBSurfaceWithFormat(0, 4, 4, 32, SDL_PIXELFORMAT_RGBA32);
}

/** @brief Per-test resources, heap-allocated by test_setup() and released by test_teardown(),
 * so a mid-test assert failure cannot leak them or cascade into the remaining tests in the
 * group. surface is only non-NULL while the surface is caller-owned: Kit_SetSubtitlePacketData()
 * hands ownership to the packet (Kit_FreeSubtitlePacket() frees packet->surface), so each
 * test NULLs surface at that hand-off and restores it if ownership comes back. */
typedef struct {
    Kit_SubtitlePacket *packet;
    Kit_SubtitlePacket *dst_packet;
    SDL_Surface *surface;
} TestState;

/** @brief Per-test setup: heap-allocates the zeroed TestState that test_teardown() always receives. */
static int test_setup(void **state) {
    *state = calloc(1, sizeof(TestState));
    return *state == NULL ? -1 : 0;
}

/** @brief Per-test teardown: releases whatever the TestState still holds, then the state
 * itself. Packets first (they free any surface they own), then the surface only when it is
 * still caller-owned -- never both, so no double-free is possible. Tests NULL each member
 * right after their own close, so only what an assert-longjmp left behind is released here. */
static int test_teardown(void **state) {
    TestState *ts = *state;
    if(ts == NULL)
        return 0;
    Kit_FreeSubtitlePacket(&ts->packet);
    Kit_FreeSubtitlePacket(&ts->dst_packet);
    if(ts->surface != NULL)
        SDL_FreeSurface(ts->surface);
    free(ts);
    *state = NULL;
    return 0;
}

/**
 * @brief Kit_CreateSubtitlePacket() returns a non-NULL, zeroed packet, and Kit_FreeSubtitlePacket() NULs the caller's
 * pointer.
 */
static void test_create_and_free(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->packet = Kit_CreateSubtitlePacket();

    // Assert: freshly created packet is non-NULL and starts with no surface
    assert_non_null(ts->packet);
    assert_null(ts->packet->surface);

    // Act
    Kit_FreeSubtitlePacket(&ts->packet);

    // Assert: pointer is NULLed, and freeing again is a documented no-op
    assert_null(ts->packet);
    Kit_FreeSubtitlePacket(&ts->packet);
}

/**
 * @brief Kit_SetSubtitlePacketData() stores every field it is given (pts range, position, clear flag, surface).
 */
static void test_set_packet_data(void **state) {
    TestState *ts = *state;
    // Arrange
    ts->packet = Kit_CreateSubtitlePacket();
    ts->surface = create_test_surface();
    assert_non_null(ts->surface);
    SDL_Surface *surface = ts->surface;

    // Act: the packet takes ownership of the surface here
    Kit_SetSubtitlePacketData(ts->packet, true, 1.5, 2.5, 10, 20, ts->surface);
    ts->surface = NULL;

    // Assert
    assert_true(ts->packet->clear);
    assert_true(ts->packet->pts_start == 1.5);
    assert_true(ts->packet->pts_end == 2.5);
    assert_int_equal(ts->packet->x, 10);
    assert_int_equal(ts->packet->y, 20);
    assert_ptr_equal(ts->packet->surface, surface);

    Kit_FreeSubtitlePacket(&ts->packet);
}

/**
 * @brief Kit_DelSubtitlePacketRefs(packet, false) zeroes the packet's fields while leaving the caller-owned surface
 * untouched.
 */
static void test_ref_create_and_del(void **state) {
    TestState *ts = *state;
    // Arrange: the packet takes ownership of the surface in Set
    ts->packet = Kit_CreateSubtitlePacket();
    ts->surface = create_test_surface();
    assert_non_null(ts->surface);
    SDL_Surface *surface = ts->surface;
    Kit_SetSubtitlePacketData(ts->packet, false, 0.0, 1.0, 0, 0, ts->surface);
    ts->surface = NULL;

    // Act: drop the packet's refs without freeing the surface -- ownership returns to the caller
    Kit_DelSubtitlePacketRefs(ts->packet, false);
    ts->surface = surface;

    // Assert: packet was reset, but the surface is still alive and usable
    assert_null(ts->packet->surface);
    assert_int_equal(surface->w, 4);
    assert_int_equal(surface->h, 4);

    SDL_FreeSurface(ts->surface);
    ts->surface = NULL;
    Kit_FreeSubtitlePacket(&ts->packet);
}

/**
 * @brief Kit_MoveSubtitlePacketRefs(dst, src) transfers surface ownership from src (left zeroed) to dst.
 */
static void test_move_refs(void **state) {
    TestState *ts = *state;
    // Arrange: the src packet takes ownership of the surface in Set
    ts->packet = Kit_CreateSubtitlePacket();
    ts->dst_packet = Kit_CreateSubtitlePacket();
    ts->surface = create_test_surface();
    assert_non_null(ts->surface);
    SDL_Surface *surface = ts->surface;
    Kit_SetSubtitlePacketData(ts->packet, true, 3.0, 4.0, 5, 6, ts->surface);
    ts->surface = NULL;

    // Act
    Kit_MoveSubtitlePacketRefs(ts->dst_packet, ts->packet);

    // Assert: src is cleared, dst now owns the surface and the other fields
    assert_null(ts->packet->surface);
    assert_false(ts->packet->clear);
    assert_true(ts->packet->pts_start == 0.0);
    assert_ptr_equal(ts->dst_packet->surface, surface);
    assert_true(ts->dst_packet->clear);
    assert_true(ts->dst_packet->pts_start == 3.0);
    assert_true(ts->dst_packet->pts_end == 4.0);
    assert_int_equal(ts->dst_packet->x, 5);
    assert_int_equal(ts->dst_packet->y, 6);

    // A single free at teardown must be enough: src holds no surface anymore.
    Kit_FreeSubtitlePacket(&ts->packet);
    Kit_FreeSubtitlePacket(&ts->dst_packet);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_create_and_free, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_set_packet_data, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_ref_create_and_del, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_move_refs, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
