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
 * The input_t aggregate initializer previously omitted the native_pen_touch
 * member, so it was value-initialized to false despite the surrounding comment
 * suggesting true. This test guards the corrected default.
 */
TEST(ConfigInputTest, NativePenTouchDefaultsToEnabled) {
  EXPECT_TRUE(config::input.native_pen_touch);
}
