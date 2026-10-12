/**
 * @file tests/unit/test_config.cpp
 * @brief Tests for Sunshine configuration parsing, range validation, and persistence helpers.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>

// local includes
#include <src/config.h>
#include <src/file_handler.h>
#include <src/utility.h>

using namespace std::literals;

/**
 * @brief Equivalent owning strings, sized views and literals select the same configuration entries.
 */
TEST(ConfigMapTest, LooksUpOwnedKeysUsingStringsViewsAndLiterals) {
  const std::string long_key(128, 'x');
  const std::string embedded_key {"embedded\0key", 12};
  const config::settings_map_t settings {
    {"", "empty"},
    {"qp", "lowercase"},
    {"QP", "uppercase"},
    {long_key, "long"},
    {embedded_key, "embedded"},
  };
  for (const auto &key : {std::string {}, std::string {"qp"}, std::string {"QP"}, long_key, embedded_key}) {
    const auto owning = settings.find(key);
    ASSERT_NE(owning, settings.end());
    const std::string_view view {key};
    EXPECT_EQ(settings.find(view), owning);
    EXPECT_TRUE(settings.contains(view));
    EXPECT_EQ(settings.count(view), 1);
    EXPECT_EQ(settings.equal_range(view).first, owning);
  }
  EXPECT_EQ(settings.find("")->second, "empty");
  EXPECT_EQ(settings.find("qp")->second, "lowercase");
  EXPECT_EQ(settings.find("QP")->second, "uppercase");
  const std::string_view non_terminated {"qpTAIL", 2};
  EXPECT_EQ(settings.find(non_terminated), settings.find("qp"));
  EXPECT_EQ(settings.find(std::string_view {}), settings.find(""));
  EXPECT_FALSE(settings.contains("qP"));
  EXPECT_FALSE(settings.contains("missing"sv));
  EXPECT_FALSE(settings.contains("embedded\0key"));
}

/**
 * @brief Hashing uses the same complete byte sequence for every accepted key representation.
 */
TEST(ConfigMapTest, HashesEqualStringsAndSizedViewsConsistently) {
  const util::transparent_string_hash_t hash;
  for (const auto &key : {std::string {}, std::string {"qp"}, std::string(128, 'x'), std::string {"embedded\0key", 12}}) {
    EXPECT_EQ(hash(key), hash(std::string_view {key}));
    EXPECT_EQ(hash(key), std::hash<std::string> {}(key));
  }
  EXPECT_EQ(hash("qp"), hash("qp"sv));
  EXPECT_EQ(hash(""), hash(std::string_view {}));
  const config::settings_map_t::key_equal equal;
  EXPECT_TRUE(equal("qp"sv, std::string {"qp"}));
  EXPECT_FALSE(equal("qp"sv, std::string {"QP"}));
  EXPECT_FALSE(equal(std::string {"embedded\0key", 12}, "embedded"sv));
}

/**
 * @brief Parsing retains the first duplicate value, exact key case and existing whitespace/list handling.
 */
TEST(ConfigMapTest, ParsingPreservesDuplicateCaseAndWhitespaceSemantics) {
  const auto settings = config::parse_config("# retained parsing behavior\n qp = 29 \nqp = 42\nQP = 31\nlist = [first,\n second]\n");
  ASSERT_EQ(settings.size(), 3);
  EXPECT_EQ(settings.find("qp"sv)->second, "29 ");
  EXPECT_EQ(settings.find("QP")->second, "31");
  EXPECT_EQ(settings.find("list"sv)->second, "[first,\n second]");
  EXPECT_FALSE(settings.contains("missing"sv));
}

TEST(ConfigDefaultsTest, UsesAutomaticGamepadSelection) {
  const config::store_t config;
  EXPECT_EQ(config.values().input.gamepad, "auto");
}

TEST(ConfigDefaultsTest, TreatsFp16SdrCapturesAsLinear) {
  const config::store_t config;
  EXPECT_FALSE(config.values().video.fp16_sdr_gamma_encoded);
}

using NvencPresetNameParam = std::pair<int, std::string_view>;

/**
 * @brief Parameterized coverage for FFmpeg's symbolic NVENC preset names.
 */
struct NvencPresetNameTest: testing::TestWithParam<NvencPresetNameParam> {};

TEST_P(NvencPresetNameTest, UsesStableSymbolicName) {
  const auto &[quality_preset, expected] = GetParam();

  EXPECT_EQ(expected, config::nv::ffmpeg_preset_from_quality(quality_preset));
}

INSTANTIATE_TEST_SUITE_P(
  NvencQualityPresets,
  NvencPresetNameTest,
  testing::Values(
    NvencPresetNameParam {1, "p1"sv},
    NvencPresetNameParam {2, "p2"sv},
    NvencPresetNameParam {3, "p3"sv},
    NvencPresetNameParam {4, "p4"sv},
    NvencPresetNameParam {5, "p5"sv},
    NvencPresetNameParam {6, "p6"sv},
    NvencPresetNameParam {7, "p7"sv}
  )
);

namespace {

  /**
   * @brief Fixture that redirects configuration persistence to a temporary file.
   */
  class ConfigPersistenceTest: public testing::Test {
  protected:
    /**
     * @brief Prepare this store's empty temporary configuration path.
     */
    void SetUp() override {
      config_file_ = std::filesystem::temp_directory_path() / "sunshine_test_config_persistence.conf";  // NOSONAR(cpp:S5443): safe for tests
      std::filesystem::remove(config_file_);
      config_.edit_startup().sunshine.config_file = config_file_.string();
      config_.edit_startup().input.gamepad_driver.clear();
    }

    /**
     * @brief Remove files created by this isolated configuration store.
     */
    void TearDown() override {
      std::filesystem::remove(config_file_.parent_path() / "sunshine_test_config_apps.json");
      std::filesystem::remove(config_file_);
    }

    /**
     * @brief Get the temporary configuration file used by the test.
     *
     * @return Temporary configuration file path.
     */
    const std::filesystem::path &config_file() const {
      return config_file_;
    }

    /**
     * @brief Redirect applications configuration to this fixture's existing temporary file.
     */
    void prepare_apps_file() {
      const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
      ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
      config_.edit_startup().stream.file_apps = apps_file.string();
    }

    /**
     * @brief Access the configuration store retained by this persistence fixture.
     * @return Fixture-owned store whose settings and persistence policy the test exercises.
     */
    config::store_t &store() {
      return config_;
    }

  private:
    config::store_t config_;  ///< Configuration owner isolated to this test.
    std::filesystem::path config_file_;  ///< Temporary configuration file used by the test.
  };

}  // namespace

TEST_F(ConfigPersistenceTest, AppendsMissingOptionWithoutReplacingExistingText) {
  ASSERT_EQ(file_handler::write_file(config_file().string().c_str(), "# retained comment"), 0);

  EXPECT_TRUE(store().persist_config_option_if_missing("gamepad_driver", "all"));
  EXPECT_EQ(
    file_handler::read_file(config_file().string().c_str()),
    "# retained comment\ngamepad_driver = all\n"
  );

  EXPECT_FALSE(store().persist_config_option_if_missing("gamepad_driver", "virtualhid"));
  EXPECT_EQ(
    file_handler::read_file(config_file().string().c_str()),
    "# retained comment\ngamepad_driver = all\n"
  );
}

TEST_F(ConfigPersistenceTest, DoesNotReplaceAnExistingOption) {
  ASSERT_EQ(file_handler::write_file(config_file().string().c_str(), "gamepad_driver = vigembus\n"), 0);

  EXPECT_FALSE(store().persist_config_option_if_missing("gamepad_driver", "all"));
  EXPECT_EQ(file_handler::read_file(config_file().string().c_str()), "gamepad_driver = vigembus\n");
}

/**
 * @brief Persistence checks only the supplied key view and keeps case-sensitive configuration behavior.
 */
TEST_F(ConfigPersistenceTest, UsesTheSuppliedKeyViewExtentWhenDetectingExistingOptions) {
  ASSERT_EQ(file_handler::write_file(config_file().string().c_str(), "gamepad_driver = vigembus\n"), 0);
  const std::string_view key {"gamepad_driver_suffix", 14};
  EXPECT_FALSE(store().persist_config_option_if_missing(key, "all"));
  EXPECT_EQ(file_handler::read_file(config_file().string().c_str()), "gamepad_driver = vigembus\n");
  EXPECT_TRUE(store().persist_config_option_if_missing("Gamepad_driver"sv, "none"));
  EXPECT_EQ(file_handler::read_file(config_file().string().c_str()), "gamepad_driver = vigembus\nGamepad_driver = none\n");
}

TEST_F(ConfigPersistenceTest, SelectsAllDriversOnlyForLicensedUsersWithoutAPreference) {
  EXPECT_FALSE(store().select_all_gamepad_drivers_if_licensed(false));
  EXPECT_TRUE(store().edit_startup().input.gamepad_driver.empty());
  EXPECT_TRUE(file_handler::read_file(config_file().string().c_str()).empty());

  EXPECT_TRUE(store().select_all_gamepad_drivers_if_licensed(true));
  EXPECT_EQ(store().edit_startup().input.gamepad_driver, config::GAMEPAD_DRIVER_ALL);
  EXPECT_EQ(file_handler::read_file(config_file().string().c_str()), "gamepad_driver = all\n");

  EXPECT_FALSE(store().select_all_gamepad_drivers_if_licensed(true));
  EXPECT_EQ(file_handler::read_file(config_file().string().c_str()), "gamepad_driver = all\n");
}

TEST_F(ConfigPersistenceTest, AcceptsAllGamepadProfilesAcrossRepeatedConfigurationChanges) {
  const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
  ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
  store().edit_startup().stream.file_apps = apps_file.string();

  constexpr std::array profiles {"generic"sv, "x360"sv, "xone"sv, "xseries"sv, "ds4"sv, "ds5"sv, "switch"sv};
  for (const auto profile : profiles) {
    store().edit_startup().input.gamepad = "auto";
    store().apply_config_for_test(std::string {"gamepad = "} + std::string {profile} + "\n");
    EXPECT_EQ(store().edit_startup().input.gamepad, profile);
  }
}

TEST_F(ConfigPersistenceTest, ParsesFp16SdrGammaEncodedOption) {
  const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
  ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
  store().edit_startup().stream.file_apps = apps_file.string();

  store().apply_config_for_test("fp16_sdr_gamma_encoded = enabled\n");
  EXPECT_TRUE(store().edit_startup().video.fp16_sdr_gamma_encoded);

  store().apply_config_for_test("fp16_sdr_gamma_encoded = disabled\n");
  EXPECT_FALSE(store().edit_startup().video.fp16_sdr_gamma_encoded);
}

TEST_F(ConfigPersistenceTest, ParsesFecPercentagesAndRejectsOutOfRangeValues) {
  const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
  ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
  store().edit_startup().stream.file_apps = apps_file.string();

  constexpr std::array cases {
    std::pair {0, 0},
    std::pair {1, 1},
    std::pair {20, 20},
    std::pair {99, 99},
    std::pair {100, 100},
    std::pair {-1, 20},
    std::pair {101, 20},
    std::pair {255, 20},
  };
  for (const auto &[setting, expected] : cases) {
    store().edit_startup().stream.fec_percentage = 20;
    store().apply_config_for_test(std::format("fec_percentage = {}\n", setting));
    EXPECT_EQ(store().edit_startup().stream.fec_percentage, expected) << setting;
  }
}

TEST_F(ConfigPersistenceTest, ParsesNvencTwoPassModesAndDefaultsUnknownValues) {
  using enum nvenc::nvenc_two_pass;
  const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
  ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
  store().edit_startup().stream.file_apps = apps_file.string();

  constexpr std::array cases {
    std::pair {"disabled"sv, disabled},
    std::pair {"quarter_res"sv, quarter_resolution},
    std::pair {"full_res"sv, full_resolution},
    std::pair {"unknown"sv, quarter_resolution},
  };
  for (const auto &[setting, expected] : cases) {
    store().edit_startup().video.nv.two_pass = full_resolution;
    store().apply_config_for_test(std::string {"nvenc_twopass = "} + std::string {setting} + "\n");
    EXPECT_EQ(store().edit_startup().video.nv.two_pass, expected) << setting;
  }
}

/**
 * @brief AMD quality names select the codec-specific SDK presets from an unset state.
 */
TEST_F(ConfigPersistenceTest, ParsesEveryAmdQualityForEachCodec) {
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  constexpr std::array cases {
    std::tuple {"balanced"sv, 0, 5, 70},
    std::tuple {"quality"sv, 2, 0, 30},
    std::tuple {"speed"sv, 1, 10, 100},
  };
  for (const auto &[value, h264, hevc, av1] : cases) {
    auto &amd = store().edit_startup().video.amd;
    amd.amd_quality_h264.reset();
    amd.amd_quality_hevc.reset();
    amd.amd_quality_av1.reset();
    store().apply_config_for_test(std::format("amd_quality = {}\n", value));
    EXPECT_EQ(amd.amd_quality_h264, h264) << value;
    EXPECT_EQ(amd.amd_quality_hevc, hevc) << value;
    EXPECT_EQ(amd.amd_quality_av1, av1) << value;
  }
}

/**
 * @brief AMD rate-control names retain each codec's distinct CBR and latency mappings.
 */
TEST_F(ConfigPersistenceTest, ParsesEveryAmdRateControlForEachCodec) {
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  constexpr std::array cases {
    std::tuple {"cbr"sv, 1, 3, 3},
    std::tuple {"cqp"sv, 0, 0, 0},
    std::tuple {"vbr_latency"sv, 3, 1, 1},
    std::tuple {"vbr_peak"sv, 2, 2, 2},
  };
  for (const auto &[value, h264, hevc, av1] : cases) {
    auto &amd = store().edit_startup().video.amd;
    amd.amd_rc_h264.reset();
    amd.amd_rc_hevc.reset();
    amd.amd_rc_av1.reset();
    store().apply_config_for_test(std::format("amd_rc = {}\n", value));
    EXPECT_EQ(amd.amd_rc_h264, h264) << value;
    EXPECT_EQ(amd.amd_rc_hevc, hevc) << value;
    EXPECT_EQ(amd.amd_rc_av1, av1) << value;
  }
}

/**
 * @brief AMD usage names select every supported profile for all three codecs.
 */
TEST_F(ConfigPersistenceTest, ParsesEveryAmdUsageForEachCodec) {
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  constexpr std::array cases {
    std::tuple {"lowlatency"sv, 2, 2, 1},
    std::tuple {"lowlatency_high_quality"sv, 5, 5, 5},
    std::tuple {"transcoding"sv, 0, 0, 0},
    std::tuple {"ultralowlatency"sv, 1, 1, 2},
    std::tuple {"webcam"sv, 3, 3, 3},
  };
  for (const auto &[value, h264, hevc, av1] : cases) {
    auto &amd = store().edit_startup().video.amd;
    amd.amd_usage_h264.reset();
    amd.amd_usage_hevc.reset();
    amd.amd_usage_av1.reset();
    store().apply_config_for_test(std::format("amd_usage = {}\n", value));
    EXPECT_EQ(amd.amd_usage_h264, h264) << value;
    EXPECT_EQ(amd.amd_usage_hevc, hevc) << value;
    EXPECT_EQ(amd.amd_usage_av1, av1) << value;
  }
}

/**
 * @brief Invalid and empty AMD policies preserve each codec's engaged or unset original.
 */
TEST_F(ConfigPersistenceTest, RejectedAmdPoliciesRetainEachCodecOptional) {
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  const std::array originals {
    std::array<std::optional<int>, 3> {91, 92, 93},
    std::array<std::optional<int>, 3> {91, std::nullopt, 93},
    std::array<std::optional<int>, 3> {std::nullopt, std::nullopt, std::nullopt},
  };
  constexpr std::array contents {
    "amd_quality = unknown\namd_rc = unknown\namd_usage = unknown\n"sv,
    "amd_quality = BALANCED\namd_rc = CBR\namd_usage = LOWLATENCY\n"sv,
    "amd_quality = auto\namd_rc = auto\namd_usage = auto\n"sv,
    "amd_quality =\namd_rc =\namd_usage =\n"sv,
    ""sv,
  };
  for (const auto &original : originals) {
    for (const auto content : contents) {
      auto &amd = store().edit_startup().video.amd;
      amd.amd_quality_h264 = original[0];
      amd.amd_quality_hevc = original[1];
      amd.amd_quality_av1 = original[2];
      amd.amd_rc_h264 = original[0];
      amd.amd_rc_hevc = original[1];
      amd.amd_rc_av1 = original[2];
      amd.amd_usage_h264 = original[0];
      amd.amd_usage_hevc = original[1];
      amd.amd_usage_av1 = original[2];
      store().apply_config_for_test(content);
      EXPECT_EQ(amd.amd_quality_h264, original[0]) << content;
      EXPECT_EQ(amd.amd_quality_hevc, original[1]) << content;
      EXPECT_EQ(amd.amd_quality_av1, original[2]) << content;
      EXPECT_EQ(amd.amd_rc_h264, original[0]) << content;
      EXPECT_EQ(amd.amd_rc_hevc, original[1]) << content;
      EXPECT_EQ(amd.amd_rc_av1, original[2]) << content;
      EXPECT_EQ(amd.amd_usage_h264, original[0]) << content;
      EXPECT_EQ(amd.amd_usage_hevc, original[1]) << content;
      EXPECT_EQ(amd.amd_usage_av1, original[2]) << content;
    }
  }
}

/**
 * @brief QSV presets use named SDK values, clear invalid selections, and retain missing ones.
 */
TEST_F(ConfigPersistenceTest, ParsesQsvPresetNamesAndClearsInvalidSelections) {
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  EXPECT_EQ(store().values().video.qsv.qsv_preset, 4);
  constexpr std::array cases {
    std::pair {"veryslow"sv, 1},
    std::pair {"slower"sv, 2},
    std::pair {"slow"sv, 3},
    std::pair {"medium"sv, 4},
    std::pair {"fast"sv, 5},
    std::pair {"faster"sv, 6},
    std::pair {"veryfast"sv, 7},
  };
  for (const auto &[value, expected] : cases) {
    store().edit_startup().video.qsv.qsv_preset.reset();
    store().apply_config_for_test(std::format("qsv_preset = {}\n", value));
    EXPECT_EQ(store().values().video.qsv.qsv_preset, expected) << value;
  }
  for (const auto value : {"unknown"sv, "FAST"sv, "4"sv, "superfast"sv}) {
    store().edit_startup().video.qsv.qsv_preset = 99;
    store().apply_config_for_test(std::format("qsv_preset = {}\n", value));
    EXPECT_FALSE(store().values().video.qsv.qsv_preset.has_value()) << value;
  }
  for (const auto original : {std::optional<int> {99}, std::optional<int> {}}) {
    for (const auto content : {""sv, "qsv_preset =\n"sv}) {
      store().edit_startup().video.qsv.qsv_preset = original;
      store().apply_config_for_test(content);
      EXPECT_EQ(store().values().video.qsv.qsv_preset, original) << content;
    }
  }
}

/**
 * @brief Software preset names map to SVT speeds and unknown nonempty names use superfast.
 */
TEST_F(ConfigPersistenceTest, ParsesAllSoftwarePresetSpeedsAndDefaultsUnknownValues) {
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  EXPECT_EQ(store().values().video.sw.sw_preset, "superfast");
  EXPECT_EQ(store().values().video.sw.svtav1_preset, 11);
  constexpr std::array cases {
    std::pair {"veryslow"sv, 1},
    std::pair {"slower"sv, 2},
    std::pair {"slow"sv, 4},
    std::pair {"medium"sv, 5},
    std::pair {"fast"sv, 7},
    std::pair {"faster"sv, 9},
    std::pair {"veryfast"sv, 10},
    std::pair {"superfast"sv, 11},
    std::pair {"ultrafast"sv, 12},
    std::pair {"unknown"sv, 11},
    std::pair {"FAST"sv, 11},
    std::pair {"7"sv, 11},
  };
  for (const auto &[value, expected] : cases) {
    store().edit_startup().video.sw.svtav1_preset = 99;
    store().apply_config_for_test(std::format("sw_preset = {}\n", value));
    EXPECT_EQ(store().values().video.sw.sw_preset, value);
    EXPECT_EQ(store().values().video.sw.svtav1_preset, expected) << value;
  }
}

/**
 * @brief An empty software preset retains SVT state while stored nonempty names are reapplied.
 */
TEST_F(ConfigPersistenceTest, ReappliesStoredSoftwarePresetsAndRetainsEmptySelections) {
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  for (const auto original : {std::optional<int> {99}, std::optional<int> {}}) {
    store().edit_startup().video.sw.sw_preset.clear();
    store().edit_startup().video.sw.svtav1_preset = original;
    store().apply_config_for_test("");
    EXPECT_TRUE(store().values().video.sw.sw_preset.empty());
    EXPECT_EQ(store().values().video.sw.svtav1_preset, original);

    store().edit_startup().video.sw.sw_preset.clear();
    store().edit_startup().video.sw.svtav1_preset = original;
    store().apply_config_for_test("sw_preset =\n");
    EXPECT_TRUE(store().values().video.sw.sw_preset.empty());
    EXPECT_EQ(store().values().video.sw.svtav1_preset, original);
  }
  for (const auto &[value, expected] : {std::pair {"fast"sv, 7}, std::pair {"unknown"sv, 11}}) {
    for (const auto content : {"qp = 33\n"sv, "sw_preset =\n"sv}) {
      store().edit_startup().video.sw.sw_preset = value;
      store().edit_startup().video.sw.svtav1_preset = 99;
      store().apply_config_for_test(content);
      EXPECT_EQ(store().values().video.sw.sw_preset, value);
      EXPECT_EQ(store().values().video.sw.svtav1_preset, expected) << value;
    }
  }
}

/**
 * @brief Display modes recognize exact policy names and auto aliases, disabling invalid values.
 */
TEST_F(ConfigPersistenceTest, ParsesDisplayPreparationModesAndDisablesInvalidValues) {
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  const auto &dd = store().values().video.dd;
  {
    using enum config::video_t::dd_t::config_option_e;
    constexpr std::array cases {
      std::pair {"disabled"sv, disabled},
      std::pair {"verify_only"sv, verify_only},
      std::pair {"ensure_active"sv, ensure_active},
      std::pair {"ensure_primary"sv, ensure_primary},
      std::pair {"ensure_only_display"sv, ensure_only_display},
      std::pair {"unknown"sv, disabled},
      std::pair {"ENSURE_ACTIVE"sv, disabled},
      std::pair {"auto"sv, disabled},
    };
    for (const auto &[value, expected] : cases) {
      store().edit_startup().video.dd.configuration_option = ensure_primary;
      store().apply_config_for_test(std::format("dd_configuration_option = {}\n", value));
      EXPECT_EQ(dd.configuration_option, expected) << value;
    }
  }
  {
    using enum config::video_t::dd_t::resolution_option_e;
    constexpr std::array cases {
      std::pair {"disabled"sv, disabled},
      std::pair {"auto"sv, automatic},
      std::pair {"manual"sv, manual},
      std::pair {"unknown"sv, disabled},
      std::pair {"AUTO"sv, disabled},
      std::pair {"automatic"sv, disabled},
    };
    for (const auto &[value, expected] : cases) {
      store().edit_startup().video.dd.resolution_option = manual;
      store().apply_config_for_test(std::format("dd_resolution_option = {}\n", value));
      EXPECT_EQ(dd.resolution_option, expected) << value;
    }
  }
  {
    using enum config::video_t::dd_t::refresh_rate_option_e;
    constexpr std::array cases {
      std::pair {"disabled"sv, disabled},
      std::pair {"auto"sv, automatic},
      std::pair {"manual"sv, manual},
      std::pair {"unknown"sv, disabled},
      std::pair {"AUTO"sv, disabled},
      std::pair {"automatic"sv, disabled},
    };
    for (const auto &[value, expected] : cases) {
      store().edit_startup().video.dd.refresh_rate_option = manual;
      store().apply_config_for_test(std::format("dd_refresh_rate_option = {}\n", value));
      EXPECT_EQ(dd.refresh_rate_option, expected) << value;
    }
  }
  {
    using enum config::video_t::dd_t::hdr_option_e;
    constexpr std::array cases {
      std::pair {"disabled"sv, disabled},
      std::pair {"auto"sv, automatic},
      std::pair {"unknown"sv, disabled},
      std::pair {"AUTO"sv, disabled},
      std::pair {"automatic"sv, disabled},
      std::pair {"manual"sv, disabled},
    };
    for (const auto &[value, expected] : cases) {
      store().edit_startup().video.dd.hdr_option = automatic;
      store().apply_config_for_test(std::format("dd_hdr_option = {}\n", value));
      EXPECT_EQ(dd.hdr_option, expected) << value;
    }
  }
}

/**
 * @brief Missing or empty display modes retain the owner's configured policies.
 */
TEST_F(ConfigPersistenceTest, MissingOrEmptyDisplayModesRetainConfiguredValues) {
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  for (const auto content : {""sv, "dd_configuration_option =\ndd_resolution_option =\ndd_refresh_rate_option =\ndd_hdr_option =\n"sv}) {
    auto &dd = store().edit_startup().video.dd;
    dd.configuration_option = config::video_t::dd_t::config_option_e::ensure_primary;
    dd.resolution_option = config::video_t::dd_t::resolution_option_e::manual;
    dd.refresh_rate_option = config::video_t::dd_t::refresh_rate_option_e::manual;
    dd.hdr_option = config::video_t::dd_t::hdr_option_e::automatic;
    store().apply_config_for_test(content);
    EXPECT_EQ(dd.configuration_option, config::video_t::dd_t::config_option_e::ensure_primary);
    EXPECT_EQ(dd.resolution_option, config::video_t::dd_t::resolution_option_e::manual);
    EXPECT_EQ(dd.refresh_rate_option, config::video_t::dd_t::refresh_rate_option_e::manual);
    EXPECT_EQ(dd.hdr_option, config::video_t::dd_t::hdr_option_e::automatic);
  }
}

TEST(ConfigStoreTest, StartupSettingsAndRecordedChangesBelongToOneStore) {
  config::store_t first;
  config::store_t second;
  first.edit_startup().input.gamepad = "ds5";
  first.edit_startup().video.qp = 42;
  first.edit_startup().input.keybindings[0x41] = 0x42;
  first.log_config_settings({{"qp", "42"}}, false);
  EXPECT_TRUE(first.values().modified_config_settings.empty());
  first.log_config_settings({{"qp", "42"}}, true);

  EXPECT_EQ(first.values().input.gamepad, "ds5");
  EXPECT_EQ(first.values().video.qp, 42);
  EXPECT_EQ(first.values().modified_config_settings.at("qp"), "42");
  EXPECT_EQ(second.values().input.gamepad, "auto");
  EXPECT_EQ(second.values().video.qp, 28);
  EXPECT_FALSE(second.values().input.keybindings.contains(0x41));
  EXPECT_TRUE(second.values().modified_config_settings.empty());
}

TEST(ConfigStoreTest, CredentialsAreIndependentSnapshots) {
  config::store_t first;
  config::store_t second;
  EXPECT_TRUE(first.credentials_snapshot().username.empty());
  first.update_credentials({"alice", "hash-a", "salt-a"});
  auto snapshot = first.credentials_snapshot();
  snapshot.username = "mutated";
  first.update_credentials({"bob", "hash-b", "salt-b"});

  EXPECT_EQ(first.credentials_snapshot().username, "bob");
  EXPECT_EQ(first.credentials_snapshot().password, "hash-b");
  EXPECT_EQ(first.credentials_snapshot().salt, "salt-b");
  EXPECT_TRUE(second.credentials_snapshot().username.empty());
  EXPECT_EQ(snapshot.password, "hash-a");
}

TEST(ConfigStoreTest, ConcurrentCredentialReadersSeeCompleteUpdates) {
  config::store_t config;
  config.update_credentials({"alice", "alice", "alice"});
  std::atomic<bool> done {false};
  std::atomic<bool> consistent {true};
  std::thread writer {[&config, &done]() {
    for (int index = 0; index < 1000; ++index) {
      const auto value = index % 2 ? "alice" : "bob";
      config.update_credentials({value, value, value});
    }
    done = true;
  }};
  do {
    const auto snapshot = config.credentials_snapshot();
    if (snapshot.username != snapshot.password || snapshot.password != snapshot.salt) {
      consistent = false;
    }
  } while (!done);
  writer.join();
  EXPECT_TRUE(consistent);
}

TEST_F(ConfigPersistenceTest, CommandLineOverridesFileAndLastRepeatedValueWins) {
  const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
  ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
  store().edit_startup().stream.file_apps = apps_file.string();
  ASSERT_EQ(file_handler::write_file(config_file().string().c_str(), "qp = 31\nport = 47990\n"), 0);
  std::array<std::string, 4> arguments {"sunshine", config_file().string(), "qp=37", "qp=39"};
  std::array<char *, 4> argv;
  std::ranges::transform(arguments, argv.begin(), [](auto &argument) {
    return argument.data();
  });

  ASSERT_EQ(store().parse(argv.size(), argv.data()), 0);
  EXPECT_EQ(store().values().video.qp, 39);
  EXPECT_EQ(store().values().sunshine.port, 47990);
  EXPECT_EQ(store().values().modified_config_settings.at("qp"), "39");
}

TEST_F(ConfigPersistenceTest, CommandArgumentsAndFlagsStayWithTheirStore) {
  const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
  ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
  store().edit_startup().stream.file_apps = apps_file.string();
  std::array<std::string, 4> arguments {"sunshine", "-01", "--version", "argument"};
  std::array<char *, 4> argv;
  std::ranges::transform(arguments, argv.begin(), [](auto &argument) {
    return argument.data();
  });

  ASSERT_EQ(store().parse(argv.size(), argv.data()), 0);
  EXPECT_TRUE(store().values().sunshine.flags[config::flag::PIN_STDIN]);
  EXPECT_TRUE(store().values().sunshine.flags[config::flag::FRESH_STATE]);
  EXPECT_EQ(store().values().sunshine.cmd.name, "version");
  EXPECT_EQ(store().values().sunshine.cmd.argc, 1);
  EXPECT_EQ(store().values().sunshine.cmd.argv[0], arguments.back());
  const config::store_t second;
  EXPECT_FALSE(second.values().sunshine.flags.any());
  EXPECT_TRUE(second.values().sunshine.cmd.name.empty());
}

TEST(ConfigStoreTest, HelpAndUnknownFlagsRetainTheirReturnCodes) {
  config::store_t config;
  std::array<std::string, 2> arguments {"sunshine", "--help"};
  std::array<char *, 2> argv {arguments[0].data(), arguments[1].data()};
  EXPECT_EQ(config.parse(argv.size(), argv.data()), 1);
  arguments[1] = "-z";
  argv[1] = arguments[1].data();
  EXPECT_EQ(config.parse(argv.size(), argv.data()), -1);
}

TEST_F(ConfigPersistenceTest, AppliesVideoAndInputPoliciesWithoutChangingOtherStores) {
  using namespace std::chrono_literals;
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  const config::store_t untouched;
  store().apply_config_for_test(
    "qp = 33\n"
    "sw_preset = fast\n"
    "nvenc_preset = 7\n"
    "amd_quality = quality\n"
    "amd_rc = cqp\n"
    "amd_usage = lowlatency\n"
    "vaapi_quality = quality\n"
    "vaapi_rc = cqp\n"
    "vk_quality = speed\n"
    "dd_config_revert_delay = 123\n"
    "max_bitrate = 12345\n"
    "key_rightalt_to_key_win = enabled\n"
    "back_button_timeout = 42\n"
    "key_repeat_frequency = 25\n"
    "key_repeat_delay = 8\n"
    "mouse = disabled\n"
    "keyboard = disabled\n"
    "controller = disabled\n"
    "gamepad_driver = none\n"
    "gamepad = auto\n"
  );

  const auto &settings = store().values();
  EXPECT_EQ(settings.video.qp, 33);
  EXPECT_EQ(settings.video.sw.sw_preset, "fast");
  EXPECT_EQ(settings.video.sw.svtav1_preset, 7);
  EXPECT_EQ(settings.video.nv.quality_preset, 7);
#if !defined(__ANDROID__) && !defined(__APPLE__)
  EXPECT_EQ(settings.video.nv_legacy.preset, "p7");
#endif
  EXPECT_EQ(settings.video.vaapi.vaapi_quality, 3);
  EXPECT_EQ(settings.video.vk.quality, 1);
  EXPECT_EQ(settings.video.dd.config_revert_delay, 123ms);
  EXPECT_EQ(settings.video.max_bitrate, 12345);
  EXPECT_EQ(settings.input.keybindings.at(0xA5), 0x5B);
  EXPECT_EQ(settings.input.back_button_timeout, 42ms);
  EXPECT_EQ(settings.input.key_repeat_period, std::chrono::duration<double> {0.04});
  EXPECT_EQ(settings.input.key_repeat_delay, 8ms);
  EXPECT_FALSE(settings.input.mouse);
  EXPECT_FALSE(settings.input.keyboard);
  EXPECT_FALSE(settings.input.controller);
  EXPECT_EQ(settings.input.gamepad_driver, config::GAMEPAD_DRIVER_NONE);
  EXPECT_NE(untouched.values().video.qp, settings.video.qp);
  EXPECT_TRUE(untouched.values().input.mouse);
  EXPECT_TRUE(untouched.values().input.keyboard);
  EXPECT_TRUE(untouched.values().input.controller);
}

TEST_F(ConfigPersistenceTest, MissingAndRejectedPoliciesRetainExistingValues) {
  using namespace std::chrono_literals;
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  auto &settings = store().edit_startup();
  settings.video.sw.sw_preset.clear();
  settings.video.sw.svtav1_preset = 9;
  settings.video.dd.config_revert_delay = 37ms;
  settings.input.back_button_timeout = 41ms;
  settings.input.key_repeat_delay = 43ms;
  settings.input.key_repeat_period = std::chrono::duration<double> {0.2};
  store().apply_config_for_test("dd_config_revert_delay = -1\nkey_repeat_delay = -1\nkey_repeat_frequency = 0\n");

  EXPECT_EQ(settings.video.sw.svtav1_preset, 9);
  EXPECT_EQ(settings.video.dd.config_revert_delay, 37ms);
  EXPECT_EQ(settings.input.back_button_timeout, 41ms);
  EXPECT_EQ(settings.input.key_repeat_delay, 43ms);
  EXPECT_EQ(settings.input.key_repeat_period, std::chrono::duration<double> {0.2});
  EXPECT_FALSE(settings.input.key_rightalt_to_key_win);
}

TEST_F(ConfigPersistenceTest, LoggingThresholdAcceptsNamesAndDigitsAndRetainsUnknownValues) {
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  const std::array cases {
    std::pair {"verbose"sv, 0},
    std::pair {"debug"sv, 1},
    std::pair {"info"sv, 2},
    std::pair {"warning"sv, 3},
    std::pair {"error"sv, 4},
    std::pair {"fatal"sv, 5},
    std::pair {"none"sv, 6},
    std::pair {"0"sv, 0},
    std::pair {"6"sv, 6},
    std::pair {"7"sv, 2},
    std::pair {"unknown"sv, 2},
    std::pair {""sv, 2},
  };
  for (const auto &[value, expected] : cases) {
    store().edit_startup().sunshine.min_log_level = 2;
    store().apply_config_for_test(std::format("min_log_level = {}\n", value));
    EXPECT_EQ(store().values().sunshine.min_log_level, expected) << value;
  }
}

TEST(ConfigStoreTest, RejectsMalformedOverridesBeforeOpeningConfiguration) {
  config::store_t config;
  std::array<std::string, 2> arguments {"sunshine", "=value"};
  std::array<char *, 2> argv {arguments[0].data(), arguments[1].data()};

  EXPECT_EQ(config.parse(argv.size(), argv.data()), -1);
}

#ifdef _WIN32
TEST_F(ConfigPersistenceTest, VigEmPolicyKeepsCompatibleProfilesAndResetsUnsupportedOnes) {
  ASSERT_NO_FATAL_FAILURE(prepare_apps_file());
  const std::array cases {
    std::pair {"auto"sv, "auto"sv},
    std::pair {"x360"sv, "x360"sv},
    std::pair {"ds4"sv, "ds4"sv},
    std::pair {"ds5"sv, "auto"sv},
  };
  for (const auto &[profile, expected] : cases) {
    store().apply_config_for_test(std::format("gamepad_driver = vigembus\ngamepad = {}\n", profile));
    EXPECT_EQ(store().values().input.gamepad_driver, config::GAMEPAD_DRIVER_VIGEMBUS);
    EXPECT_EQ(store().values().input.gamepad, expected);
  }
  store().apply_config_for_test("gamepad_driver = all\ngamepad = ds5\n");
  EXPECT_EQ(store().values().input.gamepad_driver, config::GAMEPAD_DRIVER_ALL);
  EXPECT_EQ(store().values().input.gamepad, "ds5");
}

TEST_F(ConfigPersistenceTest, HelpAfterShortcutModesAvoidsServiceAndConfigurationSideEffects) {
  std::array<std::string, 4> arguments {"sunshine", "--shortcut", "--shortcut-admin", "--help"};
  std::array<char *, 4> argv;
  std::ranges::transform(arguments, argv.begin(), [](auto &argument) {
    return argument.data();
  });

  EXPECT_EQ(store().parse(argv.size(), argv.data()), 1);
  EXPECT_FALSE(std::filesystem::exists(config_file()));
  EXPECT_TRUE(store().values().sunshine.cmd.name.empty());
}
#endif
