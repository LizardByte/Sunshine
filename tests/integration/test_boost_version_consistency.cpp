/**
 * @file tests/integration/test_boost_version_consistency.cpp
 * @brief Tests for the Boost system-version and fallback-version contracts.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <format>
#include <regex>
#include <string>
#include <string_view>

// local includes
#include "src/file_handler.h"

namespace {
  /**
   * @brief Read a project file copied into the test runtime directory.
   *
   * @param path Project-relative file path.
   * @return File contents.
   */
  std::string read_project_file(const std::string_view path) {
    const auto path_string = std::format("{}/{}", SUNSHINE_TEST_BIN_DIR, path);
    auto content = file_handler::read_file(path_string.c_str());
    EXPECT_FALSE(content.empty()) << "Unable to read " << path;
    return content;
  }

  /**
   * @brief Extract the first capture group from text.
   *
   * @param content Text to search.
   * @param pattern Regular expression containing a capture group.
   * @return The first captured value, or an empty string when no match exists.
   */
  std::string extract_value(const std::string &content, const std::regex &pattern) {
    std::smatch match;
    EXPECT_TRUE(std::regex_search(content, match, pattern));
    return match.size() > 1 ? match[1].str() : std::string {};
  }
}  // namespace

TEST(BoostVersionConsistencyTest, SystemPackageAllowsCompatibleVersions) {
  const auto dependency = read_project_file("cmake/dependencies/Boost_Sunshine.cmake");
  const auto documentation = read_project_file("docs/building.md");
  const auto macos_definitions = read_project_file("cmake/compile_definitions/macos.cmake");
  const auto common_macros = read_project_file("cmake/macros/common.cmake");
  const auto minimum_version = extract_value(
    dependency,
    std::regex {R"regex(set\(BOOST_MINIMUM_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\))regex"}
  );

  EXPECT_NE(
    dependency.find("find_package(Boost ${BOOST_MINIMUM_VERSION} CONFIG COMPONENTS ${BOOST_COMPONENTS})"),
    std::string::npos
  );
  EXPECT_EQ(dependency.find("EXACT"), std::string::npos);
  EXPECT_EQ(dependency.find("FetchContent"), std::string::npos);
  EXPECT_NE(dependency.find("CPMGetPackage(Boost)"), std::string::npos);
  EXPECT_NE(documentation.find(std::format("Boost `{}` or newer", minimum_version)), std::string::npos);
  EXPECT_EQ(macos_definitions.find("FETCH_CONTENT_BOOST_USED"), std::string::npos);
  EXPECT_NE(macos_definitions.find("CPM_BOOST_USED"), std::string::npos);
  EXPECT_EQ(common_macros.find("FETCH_CONTENT_BOOST_USED"), std::string::npos);
  EXPECT_NE(common_macros.find("CPM_BOOST_USED"), std::string::npos);
}

TEST(BoostVersionConsistencyTest, CpmFallbackMatchesFlatpakSource) {
  const auto package_lock = read_project_file("package-lock.cmake");
  const auto flatpak_module = read_project_file("packaging/linux/flatpak/modules/boost.json");
  const auto fallback_version = extract_value(
    package_lock,
    std::regex {R"regex(set\(BOOST_TAG boost-([0-9]+\.[0-9]+\.[0-9]+)\))regex"}
  );
  const auto fallback_hash = extract_value(
    package_lock,
    std::regex {R"regex(set\(BOOST_SHA256 ([0-9a-f]{64})\))regex"}
  );

  EXPECT_NE(
    flatpak_module.find(std::format("boost-{0}/boost-{0}-cmake.tar.xz", fallback_version)),
    std::string::npos
  );
  EXPECT_NE(flatpak_module.find(std::format("\"sha256\": \"{}\"", fallback_hash)), std::string::npos);
}
