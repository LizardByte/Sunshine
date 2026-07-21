/**
 * @file tests/unit/platform/test_virtualhid_input.cpp
 * @brief Tests for shared libvirtualhid input helpers.
 */
#include "../../tests_common.h"

// standard includes
#include <string>
#include <string_view>
#include <utility>

// local includes
#include "src/config.h"
#include "src/platform/virtualhid_input.h"

using namespace std::literals;

namespace {

  /**
   * @brief Expected touchpad support for a configured gamepad.
   */
  struct gamepad_touchpad_case_t {
    std::string_view gamepad;  ///< Configured gamepad name.
    bool expected;  ///< Whether the configured gamepad supports touchpad input.
  };

  /**
   * @brief Parameterized fixture that restores the configured gamepad after each test.
   */
  class VirtualHidInputTest: public ::testing::TestWithParam<gamepad_touchpad_case_t> {
  protected:
    /**
     * @brief Preserve the configured gamepad.
     */
    void SetUp() override {
      original_gamepad = config::input.gamepad;
    }

    /**
     * @brief Restore the configured gamepad.
     */
    void TearDown() override {
      config::input.gamepad = std::move(original_gamepad);
    }

    std::string original_gamepad;  ///< Configured gamepad restored after each test.
  };

}  // namespace

TEST_P(VirtualHidInputTest, ReportsExpectedTouchpadSupport) {
  const auto &[gamepad, expected] = GetParam();
  config::input.gamepad = gamepad;
  EXPECT_EQ(platf::virtualhid::configured_gamepad_supports_touchpad(), expected) << gamepad;
}

INSTANTIATE_TEST_SUITE_P(
  ConfiguredGamepads,
  VirtualHidInputTest,
  ::testing::Values(
    gamepad_touchpad_case_t {"auto"sv, true},
    gamepad_touchpad_case_t {"generic"sv, false},
    gamepad_touchpad_case_t {"x360"sv, false},
    gamepad_touchpad_case_t {"xone"sv, false},
    gamepad_touchpad_case_t {"xseries"sv, false},
    gamepad_touchpad_case_t {"ds4"sv, true},
    gamepad_touchpad_case_t {"ds5"sv, true},
    gamepad_touchpad_case_t {"switch"sv, false}
  ),
  [](const ::testing::TestParamInfo<gamepad_touchpad_case_t> &info) {
    return std::string {info.param.gamepad};
  }
);
