/**
 * @file tests/unit/test_config.cpp
 * @brief Test src/config.* configuration parsing helpers.
 */
// test imports
#include "../tests_common.h"

// standard imports
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// local imports
#include <src/config.h>

// internal helpers, not exposed in config.h
namespace config {
  void list_prep_cmd_f(std::unordered_map<std::string, std::string> &vars, const std::string &name, std::vector<prep_cmd_t> &input);

  namespace dd {
    video_t::dd_t::mode_remapping_t mode_remapping_from_view(std::string_view value);
  }  // namespace dd
}  // namespace config

struct ConfigParsingTest: BaseTest {};

TEST_F(ConfigParsingTest, PrepCmdMalformedJsonDoesNotThrow) {
  // bad json here must not kill startup
  std::unordered_map<std::string, std::string> vars {{"global_prep_cmd", "["}};
  std::vector<config::prep_cmd_t> input;

  EXPECT_NO_THROW(config::list_prep_cmd_f(vars, "global_prep_cmd", input));
  EXPECT_TRUE(input.empty());
}

TEST_F(ConfigParsingTest, PrepCmdValidJsonIsParsed) {
  std::unordered_map<std::string, std::string> vars {
    {"global_prep_cmd", R"([{"do":"echo start","undo":"echo stop","elevated":false}])"}
  };
  std::vector<config::prep_cmd_t> input;

  ASSERT_NO_THROW(config::list_prep_cmd_f(vars, "global_prep_cmd", input));
  ASSERT_EQ(input.size(), 1u);
  EXPECT_EQ(input[0].do_cmd, "echo start");
  EXPECT_EQ(input[0].undo_cmd, "echo stop");
  EXPECT_FALSE(input[0].elevated);
}

TEST_F(ConfigParsingTest, ModeRemappingMalformedJsonReturnsEmpty) {
  config::video_t::dd_t::mode_remapping_t result;
  EXPECT_NO_THROW(result = config::dd::mode_remapping_from_view("{ not valid json"));
  EXPECT_TRUE(result.mixed.empty());
  EXPECT_TRUE(result.resolution_only.empty());
  EXPECT_TRUE(result.refresh_rate_only.empty());
}

TEST_F(ConfigParsingTest, ModeRemappingValidJsonMissingChildKeysReturnsEmpty) {
  // valid json but missing keys also throws (ptree_bad_path)
  config::video_t::dd_t::mode_remapping_t result;
  EXPECT_NO_THROW(result = config::dd::mode_remapping_from_view("[]"));
  EXPECT_TRUE(result.mixed.empty());
  EXPECT_TRUE(result.resolution_only.empty());
  EXPECT_TRUE(result.refresh_rate_only.empty());
}

TEST_F(ConfigParsingTest, ModeRemappingValidJsonIsParsed) {
  constexpr std::string_view value = R"({
    "mixed": [{"requested_resolution":"1920x1080","requested_fps":"60","final_resolution":"2560x1440","final_refresh_rate":"120"}],
    "resolution_only": [],
    "refresh_rate_only": []
  })";

  config::video_t::dd_t::mode_remapping_t result;
  ASSERT_NO_THROW(result = config::dd::mode_remapping_from_view(value));
  ASSERT_EQ(result.mixed.size(), 1u);
  EXPECT_EQ(result.mixed[0].requested_resolution, "1920x1080");
  EXPECT_EQ(result.mixed[0].requested_fps, "60");
  EXPECT_EQ(result.mixed[0].final_resolution, "2560x1440");
  EXPECT_EQ(result.mixed[0].final_refresh_rate, "120");
}
