/**
 * @file tests/integration/test_virtualhid_version_consistency.cpp
 * @brief Tests for the shared Virtual HID Driver minimum-version contract.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <format>
#include <string>
#include <string_view>

// local includes
#include "src/file_handler.h"

namespace {
  /**
   * @brief Documentation pages that publish the minimum Virtual HID Driver version.
   */
  constexpr std::array<std::string_view, 2> virtualhid_version_documents {
    "docs/getting_started.md",
    "docs/troubleshooting.md",
  };
}  // namespace

TEST(VirtualHidVersionConsistencyTest, DocumentationMatchesConfiguredMinimum) {
  const auto expected_text = std::format(
    "Sunshine requires Virtual HID Driver version `{}` or newer",
    LIBVIRTUALHID_MINIMUM_VERSION
  );

  for (const auto path : virtualhid_version_documents) {
    const auto path_string = std::format("{}/{}", SUNSHINE_TEST_BIN_DIR, path);
    const auto content = file_handler::read_file(path_string.c_str());
    ASSERT_FALSE(content.empty()) << "Unable to read " << path;
    EXPECT_NE(content.find(expected_text), std::string::npos)
      << path << " must use the CMake-configured Virtual HID Driver minimum " << LIBVIRTUALHID_MINIMUM_VERSION;
  }
}
