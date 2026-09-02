/**
 * @file tests/unit/test_logging.cpp
 * @brief Test src/logging.*.
 */

// test includes
#include "../tests_common.h"
#include "../tests_log_checker.h"

// standard includes
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>

// local includes
#include <src/logging.h>

namespace {
  std::array log_levels = {
    std::tuple("verbose", &verbose),
    std::tuple("debug", &debug),
    std::tuple("info", &info),
    std::tuple("warning", &warning),
    std::tuple("error", &error),
    std::tuple("fatal", &fatal),
  };

  constexpr auto log_file = "test_sunshine.log";

  /**
   * @brief Write test content to a log file.
   *
   * @param path Path to write.
   * @param content Content to write.
   */
  void write_log_file(const std::filesystem::path &path, std::string_view content) {
    std::ofstream output {path};
    output << content;
  }

  /**
   * @brief Read all content from a test log file.
   *
   * @param path Path to read.
   * @return File content.
   */
  std::string read_log_file(const std::filesystem::path &path) {
    std::ifstream input {path};
    return {std::istreambuf_iterator<char> {input}, std::istreambuf_iterator<char> {}};
  }
}  // namespace

/**
 * @brief Test fixture for startup log rotation.
 */
class LogRotationTest: public BaseTest {
protected:
  /**
   * @brief Create an empty directory for the current test.
   */
  void SetUp() override {
    BaseTest::SetUp();
    std::filesystem::remove_all(test_directory);
    std::filesystem::create_directories(test_directory);
  }

  /**
   * @brief Remove files created by the current test.
   */
  void TearDown() override {
    std::filesystem::remove_all(test_directory);
    BaseTest::TearDown();
  }

  /**
   * @brief Build the path for a rotated test log.
   *
   * @param generation Rotated log generation number.
   * @return Path with the generation suffix appended.
   */
  std::filesystem::path rotated_log_path(std::size_t generation) const {
    auto path = log_path;
    path += std::format(".{}", generation);
    return path;
  }

  const std::filesystem::path test_directory {std::filesystem::path {SUNSHINE_TEST_BIN_DIR} / "log_rotation_tests"};  ///< Directory containing log rotation test files.
  const std::filesystem::path log_path {test_directory / "custom.log"};  ///< Path to the current test log.
};

TEST_F(LogRotationTest, RotatesCurrentLogAndRetainsFivePreviousLogs) {
  write_log_file(log_path, "current");
  for (std::size_t generation = 1; generation <= logging::retained_log_file_count; ++generation) {
    write_log_file(rotated_log_path(generation), std::to_string(generation));
  }

  EXPECT_FALSE(logging::rotate_log_file(log_path));

  EXPECT_FALSE(std::filesystem::exists(log_path));
  EXPECT_EQ(read_log_file(rotated_log_path(1)), "current");
  EXPECT_EQ(read_log_file(rotated_log_path(2)), "1");
  EXPECT_EQ(read_log_file(rotated_log_path(3)), "2");
  EXPECT_EQ(read_log_file(rotated_log_path(4)), "3");
  EXPECT_EQ(read_log_file(rotated_log_path(5)), "4");
}

TEST_F(LogRotationTest, SupportsMissingLogGenerations) {
  write_log_file(rotated_log_path(2), "second");

  EXPECT_FALSE(logging::rotate_log_file(log_path));

  EXPECT_FALSE(std::filesystem::exists(log_path));
  EXPECT_FALSE(std::filesystem::exists(rotated_log_path(1)));
  EXPECT_FALSE(std::filesystem::exists(rotated_log_path(2)));
  EXPECT_EQ(read_log_file(rotated_log_path(3)), "second");
}

TEST_F(LogRotationTest, ReportsFilesystemErrors) {
  std::filesystem::create_directories(rotated_log_path(logging::retained_log_file_count) / "child");
  write_log_file(log_path, "current");

  EXPECT_TRUE(logging::rotate_log_file(log_path));
  EXPECT_EQ(read_log_file(log_path), "current");
}

struct LogLevelsTest: BaseTest, testing::WithParamInterface<decltype(log_levels)::value_type> {};

INSTANTIATE_TEST_SUITE_P(
  Logging,
  LogLevelsTest,
  testing::ValuesIn(log_levels),
  [](const auto &info) {
    return std::string(std::get<0>(info.param));
  }
);

TEST_P(LogLevelsTest, PutMessage) {
  auto [label, plogger] = GetParam();
  ASSERT_TRUE(plogger);
  auto &logger = *plogger;

  std::random_device rand_dev;
  std::mt19937_64 rand_gen(rand_dev());
  auto test_message = std::format("{}{}", rand_gen(), rand_gen());
  BOOST_LOG(logger) << test_message;

  ASSERT_TRUE(log_checker::line_contains(log_file, test_message));
}
