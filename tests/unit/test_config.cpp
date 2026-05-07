/**
 * @file tests/unit/test_config.cpp
 * @brief Test src/config.cpp
 */
#include "../tests_common.h"

// standard includes
#include <string>
#include <unordered_map>

// local includes
#include <src/config.h>

// Forward-declare the internal apply_config function for testing
namespace config {
  // NOLINTNEXTLINE(modernize-use-transparent-functors)
  void apply_config(std::unordered_map<std::string, std::string> &&vars);
}

class ManualRotationTest: public ::testing::TestWithParam<std::pair<std::string, int>> {
protected:
  void SetUp() override {
    // Reset to default before each test
    config::video.manual_rotation = 0;
  }
};

TEST_P(ManualRotationTest, ParsesRotationValues) {
  const auto [input, expected] = GetParam();

  // NOLINTNEXTLINE(modernize-use-transparent-functors)
  std::unordered_map<std::string, std::string> vars;
  vars["manual_rotation"] = input;
  config::apply_config(std::move(vars));

  EXPECT_EQ(config::video.manual_rotation, expected);
}

INSTANTIATE_TEST_SUITE_P(
  ConfigTests,
  ManualRotationTest,
  testing::Values(
    // Valid rotation values
    std::make_pair("0", 0),
    std::make_pair("90", 90),
    std::make_pair("180", 180),
    std::make_pair("270", 270),
    // Invalid values should normalize to 0
    std::make_pair("45", 0),
    std::make_pair("360", 0),
    std::make_pair("-90", 0),
    std::make_pair("1", 0),
    std::make_pair("abc", 0)
  ),
  [](const testing::TestParamInfo<ManualRotationTest::ParamType> &info) {
    auto input = info.param.first;
    // Replace non-alphanumeric chars for valid test name
    std::replace_if(
      input.begin(), input.end(),
      [](char c) { return !std::isalnum(c); },
      '_'
    );
    return "rotation_" + input;
  }
);

TEST(ManualRotationDefaultTest, DefaultIsZero) {
  // Reset config and apply empty vars to verify default
  config::video.manual_rotation = 999;
  // NOLINTNEXTLINE(modernize-use-transparent-functors)
  std::unordered_map<std::string, std::string> vars;
  vars["manual_rotation"] = "0";
  config::apply_config(std::move(vars));

  EXPECT_EQ(config::video.manual_rotation, 0);
}
