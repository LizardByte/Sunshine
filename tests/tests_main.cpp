/**
 * @file tests/tests_main.cpp
 * @brief Entry point definition.
 */

// test includes
#include "tests_common.h"
#include "tests_environment.h"
#include "tests_events.h"

// standard includes
#include <memory>

#ifdef _WIN32
  // standard includes
  #include <cwchar>
  #include <filesystem>
  #include <fstream>
  #include <optional>

  // platform includes
  // Windows types must precede shellapi.h.
  // clang-format off
  #include <Windows.h>
  #include <shellapi.h>
  // clang-format on

  // local includes
  #include <src/platform/windows/utf_utils.h>
  #include <src/utility.h>

/**
 * @brief Record a URL delivered to a temporary test protocol handler without opening a browser.
 *
 * @return Recorder exit code, or no value for normal test invocations.
 */
std::optional<int> record_test_url() {
  int count = 0;
  auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!arguments) {
    return ERROR_NOT_ENOUGH_MEMORY;
  }
  auto free_arguments = util::fail_guard([arguments]() {
    LocalFree(arguments);
  });
  if (count < 2 || std::wcscmp(arguments[1], L"--record-url") != 0) {
    return std::nullopt;
  }
  if (count != 4) {
    return ERROR_INVALID_PARAMETER;
  }
  std::ofstream output(std::filesystem::path {arguments[2]}, std::ios::binary);
  output << utf_utils::to_utf8(arguments[3]);
  return output.good() ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
}
#endif

/**
 * @brief Run Sunshine tests, or record a URL delivered to a test protocol handler.
 *
 * @param argc Number of process arguments.
 * @param argv Process arguments forwarded to GoogleTest.
 * @return Test result or URL recorder exit code.
 */
int main(int argc, char **argv) {
#ifdef _WIN32
  if (auto result = record_test_url(); result.has_value()) {
    return *result;
  }
#endif

  testing::InitGoogleTest(&argc, argv);
  // GoogleTest takes ownership of the environment and listener after registration.
  testing::AddGlobalTestEnvironment(std::make_unique<SunshineEnvironment>().release());
  testing::UnitTest::GetInstance()->listeners().Append(std::make_unique<SunshineEventListener>().release());
  return RUN_ALL_TESTS();
}
