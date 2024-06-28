/**
 * @file tests/unit/test_logging.cpp
 * @brief Test src/logging.*.
 */
#include <fstream>

#include <src/logging.h>

#include <tests/conftest.cpp>

class LoggerInitTest: public virtual BaseTest, public ::testing::WithParamInterface<int> {
protected:
  void
  SetUp() override {
    BaseTest::SetUp();
  }

  void
  TearDown() override {
    BaseTest::TearDown();
  }
};
INSTANTIATE_TEST_SUITE_P(
  LogLevel,
  LoggerInitTest,
  ::testing::Values(
    0,
    1,
    2,
    3,
    4,
    5));
TEST_P(LoggerInitTest, InitLogging) {
  int logLevel = GetParam();
  std::string logFilePath = "test_log_" + std::to_string(logLevel) + ".log";

  // deinit the BaseTest logger
  BaseTest::deinit_guard.reset();

  auto log_deinit = logging::init(logLevel, logFilePath);
  if (!log_deinit) {
    FAIL() << "Failed to initialize logging";
  }
}

TEST(LogFlushTest, CheckLogFile) {
  // Write a log message
  BOOST_LOG(info) << "Test message";

  // Call log_flush
  logging::log_flush();

  // Check the contents of the log file
  std::ifstream log_file("test.log");
  std::string line;
  bool found = false;
  while (std::getline(log_file, line)) {
    if (line.find("Test message") != std::string::npos) {
      found = true;
      break;
    }
  }

  EXPECT_TRUE(found);
}

TEST(PrintHelpTest, CheckOutput) {
  std::string name = "test";
  logging::print_help(name.c_str());

  std::string output = cout_buffer.str();

  EXPECT_NE(output.find("Usage: " + name), std::string::npos);
  EXPECT_NE(output.find("--help"), std::string::npos);
}
