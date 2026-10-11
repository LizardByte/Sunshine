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
#include <string>
#include <thread>
#include <utility>

// local includes
#include <src/config.h>
#include <src/file_handler.h>
#include <src/utility.h>

using namespace std::literals;

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

    config::store_t config_;  ///< Configuration owner isolated to this test.

  private:
    std::filesystem::path config_file_;  ///< Temporary configuration file used by the test.
  };

}  // namespace

TEST_F(ConfigPersistenceTest, AppendsMissingOptionWithoutReplacingExistingText) {
  ASSERT_EQ(file_handler::write_file(config_file().string().c_str(), "# retained comment"), 0);

  EXPECT_TRUE(config_.persist_config_option_if_missing("gamepad_driver", "all"));
  EXPECT_EQ(
    file_handler::read_file(config_file().string().c_str()),
    "# retained comment\ngamepad_driver = all\n"
  );

  EXPECT_FALSE(config_.persist_config_option_if_missing("gamepad_driver", "virtualhid"));
  EXPECT_EQ(
    file_handler::read_file(config_file().string().c_str()),
    "# retained comment\ngamepad_driver = all\n"
  );
}

TEST_F(ConfigPersistenceTest, DoesNotReplaceAnExistingOption) {
  ASSERT_EQ(file_handler::write_file(config_file().string().c_str(), "gamepad_driver = vigembus\n"), 0);

  EXPECT_FALSE(config_.persist_config_option_if_missing("gamepad_driver", "all"));
  EXPECT_EQ(file_handler::read_file(config_file().string().c_str()), "gamepad_driver = vigembus\n");
}

TEST_F(ConfigPersistenceTest, SelectsAllDriversOnlyForLicensedUsersWithoutAPreference) {
  EXPECT_FALSE(config_.select_all_gamepad_drivers_if_licensed(false));
  EXPECT_TRUE(config_.edit_startup().input.gamepad_driver.empty());
  EXPECT_TRUE(file_handler::read_file(config_file().string().c_str()).empty());

  EXPECT_TRUE(config_.select_all_gamepad_drivers_if_licensed(true));
  EXPECT_EQ(config_.edit_startup().input.gamepad_driver, config::GAMEPAD_DRIVER_ALL);
  EXPECT_EQ(file_handler::read_file(config_file().string().c_str()), "gamepad_driver = all\n");

  EXPECT_FALSE(config_.select_all_gamepad_drivers_if_licensed(true));
  EXPECT_EQ(file_handler::read_file(config_file().string().c_str()), "gamepad_driver = all\n");
}

TEST_F(ConfigPersistenceTest, AcceptsAllGamepadProfilesAcrossRepeatedConfigurationChanges) {
  const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
  ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
  config_.edit_startup().stream.file_apps = apps_file.string();

  constexpr std::array profiles {"generic"sv, "x360"sv, "xone"sv, "xseries"sv, "ds4"sv, "ds5"sv, "switch"sv};
  for (const auto profile : profiles) {
    config_.edit_startup().input.gamepad = "auto";
    config_.apply_config_for_test(std::string {"gamepad = "} + std::string {profile} + "\n");
    EXPECT_EQ(config_.edit_startup().input.gamepad, profile);
  }
}

TEST_F(ConfigPersistenceTest, ParsesFp16SdrGammaEncodedOption) {
  const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
  ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
  config_.edit_startup().stream.file_apps = apps_file.string();

  config_.apply_config_for_test("fp16_sdr_gamma_encoded = enabled\n");
  EXPECT_TRUE(config_.edit_startup().video.fp16_sdr_gamma_encoded);

  config_.apply_config_for_test("fp16_sdr_gamma_encoded = disabled\n");
  EXPECT_FALSE(config_.edit_startup().video.fp16_sdr_gamma_encoded);
}

TEST_F(ConfigPersistenceTest, ParsesFecPercentagesAndRejectsOutOfRangeValues) {
  const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
  ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
  config_.edit_startup().stream.file_apps = apps_file.string();

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
    config_.edit_startup().stream.fec_percentage = 20;
    config_.apply_config_for_test(std::format("fec_percentage = {}\n", setting));
    EXPECT_EQ(config_.edit_startup().stream.fec_percentage, expected) << setting;
  }
}

TEST_F(ConfigPersistenceTest, ParsesNvencTwoPassModesAndDefaultsUnknownValues) {
  using enum nvenc::nvenc_two_pass;
  const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
  ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
  config_.edit_startup().stream.file_apps = apps_file.string();

  constexpr std::array cases {
    std::pair {"disabled"sv, disabled},
    std::pair {"quarter_res"sv, quarter_resolution},
    std::pair {"full_res"sv, full_resolution},
    std::pair {"unknown"sv, quarter_resolution},
  };
  for (const auto &[setting, expected] : cases) {
    config_.edit_startup().video.nv.two_pass = full_resolution;
    config_.apply_config_for_test(std::string {"nvenc_twopass = "} + std::string {setting} + "\n");
    EXPECT_EQ(config_.edit_startup().video.nv.two_pass, expected) << setting;
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
  std::thread writer {[&]() {
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
  config_.edit_startup().stream.file_apps = apps_file.string();
  ASSERT_EQ(file_handler::write_file(config_file().string().c_str(), "qp = 31\nport = 47990\n"), 0);
  std::array<std::string, 4> arguments {"sunshine", config_file().string(), "qp=37", "qp=39"};
  std::array<char *, 4> argv;
  std::ranges::transform(arguments, argv.begin(), [](auto &argument) {
    return argument.data();
  });

  ASSERT_EQ(config_.parse(argv.size(), argv.data()), 0);
  EXPECT_EQ(config_.values().video.qp, 39);
  EXPECT_EQ(config_.values().sunshine.port, 47990);
  EXPECT_EQ(config_.values().modified_config_settings.at("qp"), "39");
}

TEST_F(ConfigPersistenceTest, CommandArgumentsAndFlagsStayWithTheirStore) {
  const auto apps_file = config_file().parent_path() / "sunshine_test_config_apps.json";
  ASSERT_EQ(file_handler::write_file(apps_file.string().c_str(), "{}"), 0);
  config_.edit_startup().stream.file_apps = apps_file.string();
  std::array<std::string, 4> arguments {"sunshine", "-01", "--version", "argument"};
  std::array<char *, 4> argv;
  std::ranges::transform(arguments, argv.begin(), [](auto &argument) {
    return argument.data();
  });

  ASSERT_EQ(config_.parse(argv.size(), argv.data()), 0);
  EXPECT_TRUE(config_.values().sunshine.flags[config::flag::PIN_STDIN]);
  EXPECT_TRUE(config_.values().sunshine.flags[config::flag::FRESH_STATE]);
  EXPECT_EQ(config_.values().sunshine.cmd.name, "version");
  EXPECT_EQ(config_.values().sunshine.cmd.argc, 1);
  EXPECT_EQ(config_.values().sunshine.cmd.argv[0], arguments.back());
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
