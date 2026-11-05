/**
 * @file tests/unit/platform/windows/test_url_launcher.cpp
 * @brief Exercise Windows URL opening through a temporary per-user protocol handler.
 */

// test includes
#include "../../../tests_common.h"

#ifdef _WIN32
  // standard includes
  #include <array>
  #include <atomic>
  #include <chrono>
  #include <filesystem>
  #include <fstream>
  #include <iterator>
  #include <string>
  #include <thread>

  // platform includes
  #include <Windows.h>

  // local includes
  #include <src/platform/windows/utf_utils.h>
  #include <src/utility.h>

/**
 * @brief Register an isolated URL handler that records requests instead of opening the user's browser.
 */
class WindowsOpenUrlTest: public BaseTest {
protected:
  /**
   * @brief Mark tests that temporarily register a per-user protocol.
   *
   * @return True because these tests briefly create an isolated registry entry.
   */
  bool isSystemTest() const override {
    return true;
  }

  /**
   * @brief Register a unique protocol whose handler is the test executable's URL recorder.
   */
  void SetUp() override {
    BaseTest::SetUp();
    if (IsSkipped()) {
      return;
    }
    scheme = L"sunshine-url-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(sequence++);
    registry_path = L"Software\\Classes\\" + scheme;
    output_path = std::filesystem::path {SUNSHINE_TEST_BIN_DIR} / (scheme + L".txt");

    HKEY protocol;
    DWORD disposition;
    ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, registry_path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &protocol, &disposition), ERROR_SUCCESS);
    auto close_protocol = util::fail_guard([protocol]() {
      RegCloseKey(protocol);
    });
    if (disposition != REG_CREATED_NEW_KEY) {
      GTEST_SKIP() << "Test protocol already exists";
    }
    owns_protocol = true;
    ASSERT_EQ(RegSetValueExW(protocol, L"URL Protocol", 0, REG_SZ, reinterpret_cast<const BYTE *>(L""), sizeof(wchar_t)), ERROR_SUCCESS);

    std::array<wchar_t, 32768> executable;
    const auto length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
    ASSERT_GT(length, 0U);
    ASSERT_LT(length, executable.size());
    const auto command = L"\"" + std::wstring {executable.data(), length} + L"\" --record-url \"" + output_path.wstring() + L"\" \"%1\"";
    HKEY command_key;
    ASSERT_EQ(RegCreateKeyExW(protocol, L"shell\\open\\command", 0, nullptr, 0, KEY_WRITE, nullptr, &command_key, nullptr), ERROR_SUCCESS);
    auto close_command = util::fail_guard([command_key]() {
      RegCloseKey(command_key);
    });
    ASSERT_EQ(RegSetValueExW(command_key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE *>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))), ERROR_SUCCESS);
  }

  /**
   * @brief Remove only this fixture's temporary protocol and recorded URL.
   */
  void TearDown() override {
    if (owns_protocol) {
      EXPECT_EQ(RegDeleteTreeW(HKEY_CURRENT_USER, registry_path.c_str()), ERROR_SUCCESS);
      std::error_code error;
      std::filesystem::remove(output_path, error);
      EXPECT_FALSE(error);
    }
    BaseTest::TearDown();
  }

  /**
   * @brief Wait for asynchronous shell dispatch and verify the recorder receives the exact URL.
   *
   * @param suffix URL text following the temporary protocol name.
   */
  void expect_url_delivered(const std::wstring &suffix) {
    const auto url = utf_utils::to_utf8(scheme + suffix);
    platf::open_url(url);
    std::string received;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {10};
    do {
      std::ifstream input(output_path, std::ios::binary);
      received.assign(std::istreambuf_iterator<char> {input}, std::istreambuf_iterator<char> {});
      if (received == url) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds {20});
    } while (std::chrono::steady_clock::now() < deadline);
    EXPECT_EQ(received, url);
  }

  inline static std::atomic_uint64_t sequence = 0;  ///< Distinguishes protocols created by this test process.
  std::wstring scheme;  ///< Unique URL scheme owned by the fixture.
  std::wstring registry_path;  ///< Per-user protocol registration to remove during cleanup.
  std::filesystem::path output_path;  ///< UTF-8 URL written by the native protocol handler.
  bool owns_protocol = false;  ///< Whether setup created the protocol rather than finding an existing key.
};

/**
 * @brief Preserve Unicode URLs, query separators, percent escapes, and pairing fragments.
 */
TEST_F(WindowsOpenUrlTest, PreservesUnicodeQueriesAndFragments) {
  expect_url_delivered(L"://localhost/pin?name=\u65e5\u672c\u8a9e&text=hello%20world&value=100%25#pair");
}

/**
 * @brief Preserve spaces and shell metacharacters through the command processor's start command.
 */
TEST_F(WindowsOpenUrlTest, PreservesSpacesAndShellMetacharacters) {
  expect_url_delivered(L"://localhost/path with spaces?value=(one)^|<two>&literal=!text!#fragment");
}

/**
 * @brief Keep environment-like URL text from expanding in the command processor.
 */
TEST_F(WindowsOpenUrlTest, PreservesPercentAndExclamationDelimitedText) {
  const auto name = L"SUNSHINE_URL_TEST_" + std::to_wstring(GetCurrentProcessId());
  ASSERT_EQ(GetEnvironmentVariableW(name.c_str(), nullptr, 0), 0U);
  ASSERT_EQ(GetLastError(), ERROR_ENVVAR_NOT_FOUND);
  ASSERT_TRUE(SetEnvironmentVariableW(name.c_str(), L"unexpected-expansion"));
  auto clear_environment = util::fail_guard([&name]() {
    SetEnvironmentVariableW(name.c_str(), nullptr);
  });
  expect_url_delivered(L"://localhost/?percent=%" + name + L"%&exclamation=!" + name + L"!");
}
#endif
