/**
 * @file tests/unit/test_logging.cpp
 * @brief Test src/logging.*.
 */

// test includes
#include "../tests_common.h"
#include "../tests_log_checker.h"

// standard includes
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

// local includes
#include <src/logging.h>

namespace {
  std::array log_levels = {
    std::tuple("verbose", logging::level_e::verbose),
    std::tuple("debug", logging::level_e::debug),
    std::tuple("info", logging::level_e::info),
    std::tuple("warning", logging::level_e::warning),
    std::tuple("error", logging::level_e::error),
    std::tuple("fatal", logging::level_e::fatal),
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
  auto [label, level] = GetParam();

  std::random_device rand_dev;
  std::mt19937_64 rand_gen(rand_dev());
  auto test_message = std::format("{}{}", rand_gen(), rand_gen());
  logging::log(level, test_message);

  ASSERT_TRUE(log_checker::line_contains(log_file, test_message));
}

TEST_F(LogRotationTest, LoggingOwnerRemovesOnlyItsOwnSink) {
  const auto second_path = test_directory / "second.log";
  auto first = logging::init(0, log_path.string());
  auto second = logging::init(0, second_path.string());

  logging::info("both sinks receive this record");
  logging::log_flush();
  EXPECT_NE(read_log_file(log_path).find("both sinks receive this record"), std::string::npos);
  EXPECT_NE(read_log_file(second_path).find("both sinks receive this record"), std::string::npos);

  first.reset();
  logging::info("remaining sink receives this record");
  logging::log_flush();
  EXPECT_EQ(read_log_file(log_path).find("remaining sink receives this record"), std::string::npos);
  EXPECT_NE(read_log_file(second_path).find("remaining sink receives this record"), std::string::npos);
}

TEST_F(LogRotationTest, ManagedLoggerAcceptsConcurrentWriters) {
  auto owner = logging::init(0, log_path.string());
  std::vector<std::jthread> writers;
  for (int writer = 0; writer < 4; ++writer) {
    writers.emplace_back([writer]() {
      for (int record = 0; record < 20; ++record) {
        logging::info("writer {} record {}", writer, record);
      }
    });
  }
  writers.clear();
  owner.reset();

  const auto content = read_log_file(log_path);
  for (int writer = 0; writer < 4; ++writer) {
    for (int record = 0; record < 20; ++record) {
      EXPECT_NE(content.find(std::format("writer {} record {}", writer, record)), std::string::npos);
    }
  }
}

TEST_F(BaseTest, RejectsUnsupportedLoggerSeverity) {
  EXPECT_THROW(logging::log(static_cast<logging::level_e>(99), "invalid severity"), std::invalid_argument);
}

/**
 * @brief Runtime test severity preserves logging across shared production objects.
 */
TEST_F(LogRotationTest, ManagedLoggerPreservesRuntimeTestSeverity) {
  auto owner = logging::init(0, log_path.string());
  logging::log(logging::test_level, "runtime test severity is accepted");

  EXPECT_TRUE(log_checker::line_equals(log_path.string(), "Tests: runtime test severity is accepted"));
}
