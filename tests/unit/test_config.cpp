/**
 * @file tests/unit/test_config.cpp
 * @brief Test src/config.* default values.
 */
// test imports
#include "../tests_common.h"

// local imports
#include <src/config.h>

/**
 * @brief Verify that native_pen_touch defaults to enabled.
 *
 * The input_t aggregate initializer previously omitted the key_rightalt_to_key_win
 * member, which shifted every subsequent field and left native_pen_touch (the
 * final member) value-initialized to false despite the surrounding comment
 * suggesting true. This test guards the corrected default.
 */
TEST(ConfigInputTest, NativePenTouchDefaultsToEnabled) {
  EXPECT_TRUE(config::input.native_pen_touch);
}

/**
 * @brief Verify that key_rightalt_to_key_win defaults to disabled.
 *
 * This member is documented and configured as disabled by default; the
 * aggregate initializer now explicitly sets it to false. It was previously
 * omitted, causing the field to shift and native_pen_touch to default to false.
 */
TEST(ConfigInputTest, KeyRightAltToKeyWinDefaultsToDisabled) {
  EXPECT_FALSE(config::input.key_rightalt_to_key_win);
}
