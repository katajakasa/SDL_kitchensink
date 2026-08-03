/**
 * Unit tests for the Kit_PacketTag bit-packing helpers (kitpackettag.h).
 * These are pure inline functions operating on a union that packs a 2-bit
 * type and a 30-bit serial into a void* opaque handle stashed on AVPacket/
 * AVFrame; no library state is touched, so no Kit_Init/SDL_Init is needed.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "kitchensink3/internal/kitpackettag.h"

/**
 * @brief The serial field is 30 bits wide (see Kit_PacketTag.bits.serial), so the
 * maximum encodable value is 2^30 - 1. Kept as a named constant so the
 * boundary test below documents where it comes from.
 */
#define KIT_PACKETTAG_MAX_SERIAL ((1u << 30) - 1)

/**
 * @brief Kit_CreatePacketTag() followed by Kit_GetPacketType()/Kit_GetPacketSerial() returns exactly the packed-in
 * pair.
 */
static void test_tag_round_trip(void **state) {
    (void)state;
    // Arrange: a few representative (type, serial) pairs
    const struct {
        Kit_PacketType type;
        unsigned int serial;
    } cases[] = {
        {KIT_PACKET_TYPE_DATA, 0     },
        {KIT_PACKET_TYPE_SEEK, 1     },
        {KIT_PACKET_TYPE_EOF,  42    },
        {KIT_PACKET_TYPE_DATA, 123456},
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        // Act
        void *opaque = Kit_CreatePacketTag(cases[i].type, cases[i].serial);

        // Assert
        assert_int_equal(Kit_GetPacketType(opaque), cases[i].type);
        assert_int_equal(Kit_GetPacketSerial(opaque), cases[i].serial);
    }
}

/**
 * @brief The 30-bit serial's extreme values (0 and 2^30 - 1) survive the pack/unpack round trip exactly.
 */
static void test_tag_serial_boundaries(void **state) {
    (void)state;
    // Act / Assert: minimum serial
    void *min_tag = Kit_CreatePacketTag(KIT_PACKET_TYPE_DATA, 0);
    assert_int_equal(Kit_GetPacketType(min_tag), KIT_PACKET_TYPE_DATA);
    assert_int_equal(Kit_GetPacketSerial(min_tag), 0);

    // Act / Assert: maximum encodable serial
    void *max_tag = Kit_CreatePacketTag(KIT_PACKET_TYPE_SEEK, KIT_PACKETTAG_MAX_SERIAL);
    assert_int_equal(Kit_GetPacketType(max_tag), KIT_PACKET_TYPE_SEEK);
    assert_int_equal(Kit_GetPacketSerial(max_tag), KIT_PACKETTAG_MAX_SERIAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_tag_round_trip),
        cmocka_unit_test(test_tag_serial_boundaries),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
