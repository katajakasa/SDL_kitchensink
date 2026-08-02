/**
 * Tests for the top-level library lifecycle in kitchensink.h (Kit_Init/Kit_Quit)
 * and the Kit_ResetPlayerConfig() defaults contract.
 *
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kitchensink2/kitchensink.h"

/**
 * @brief Kit_Init/Kit_Quit must support repeated init/quit cycles in one process.
 */
static void test_init_quit_cycle(void **state) {
    (void)state;
    // Act / Assert: first init/quit cycle
    assert_int_equal(Kit_Init(0), 0);
    Kit_Quit();

    // Act / Assert
    // Re-init after quit must work
    assert_int_equal(Kit_Init(0), 0);
    Kit_Quit();
}

/**
 * @brief Kit_ResetPlayerConfig() must fill in the documented default values, which user code
 * relies on when overriding only some fields.
 */
static void test_player_config_defaults(void **state) {
    (void)state;
    // Arrange: poison the struct so untouched fields would be caught.
    Kit_PlayerConfig config;
    memset(&config, 0xFF, sizeof(config));

    // Act
    Kit_ResetPlayerConfig(&config);

    // Assert
    assert_int_equal(config.thread_count, 0);
    assert_int_equal(config.video.packet_buffer_size, 64);
    assert_int_equal(config.video.frame_buffer_size, 3);
    assert_int_equal(config.video.early_threshold, 5);
    assert_int_equal(config.video.late_threshold, 50);
    assert_int_equal(config.audio.packet_buffer_size, 64);
    assert_int_equal(config.audio.frame_buffer_size, 64);
    assert_int_equal(config.audio.early_threshold, 30);
    assert_int_equal(config.audio.late_threshold, 50);
    assert_int_equal(config.subtitle.packet_buffer_size, 64);
    assert_int_equal(config.subtitle.frame_buffer_size, 64);
    assert_int_equal(config.subtitle.font_hinting, KIT_FONT_HINTING_NONE);
    assert_int_equal(config.demuxer.read_attempts, 3);
    assert_int_equal(config.demuxer.read_retry_delay, 10);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_quit_cycle),
        cmocka_unit_test(test_player_config_defaults),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
