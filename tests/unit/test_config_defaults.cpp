/**
 * @file tests/unit/test_config_defaults.cpp
 * @brief Test default configuration values.
 */
#include "../tests_common.h"

// local includes
#include "src/config.h"

TEST(ConfigDefaultsTest, InputDefaultsKeepNativeTouchEnabled) {
  EXPECT_TRUE(config::input.keyboard);
  EXPECT_FALSE(config::input.key_rightalt_to_key_win);
  EXPECT_TRUE(config::input.mouse);
  EXPECT_TRUE(config::input.controller);
  EXPECT_TRUE(config::input.always_send_scancodes);
  EXPECT_TRUE(config::input.high_resolution_scrolling);
  EXPECT_TRUE(config::input.native_pen_touch);
  EXPECT_FALSE(config::input.touch_send_to_primary_display);
  EXPECT_EQ(config::input.touch_primary_display_rotation, "0");
}
