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
  const auto options = read_project_file("cmake/prep/options.cmake");
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
  EXPECT_EQ(dependency.find("\n        system"), std::string::npos);
  EXPECT_NE(dependency.find("CPMGetPackage(Boost)"), std::string::npos);
  EXPECT_NE(documentation.find(std::format("Boost `{}` or newer", minimum_version)), std::string::npos);
  EXPECT_EQ(macos_definitions.find("FETCH_CONTENT_BOOST_USED"), std::string::npos);
  EXPECT_NE(macos_definitions.find("CPM_BOOST_USED"), std::string::npos);
  EXPECT_EQ(common_macros.find("FETCH_CONTENT_BOOST_USED"), std::string::npos);
  EXPECT_NE(common_macros.find("CPM_BOOST_USED"), std::string::npos);
  EXPECT_NE(options.find("option(BOOST_USE_STATIC \"Use static boost libraries.\" ON)"), std::string::npos);
  EXPECT_EQ(options.find("option(BOOST_USE_STATIC \"Use static boost libraries.\" OFF)"), std::string::npos);
  EXPECT_NE(documentation.find("Static Boost libraries are preferred on every platform"), std::string::npos);
  EXPECT_NE(documentation.find("-DBOOST_USE_STATIC=OFF"), std::string::npos);
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

TEST(BoostVersionConsistencyTest, SupportedBuildsInstallStaticSystemPackages) {
  const auto documentation = read_project_file("docs/building.md");
  const auto linux_script = read_project_file("scripts/linux_build.sh");
  const auto arch_package = read_project_file("packaging/linux/Arch/PKGBUILD");
#ifdef SUNSHINE_COPR_SPEC_FIXTURE_AVAILABLE
  const auto dependency = read_project_file("cmake/dependencies/Boost_Sunshine.cmake");
  const auto copr_spec = read_project_file("packaging/linux/copr/Sunshine.spec");
  const auto minimum_version = extract_value(
    dependency,
    std::regex {R"regex(set\(BOOST_MINIMUM_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\))regex"}
  );
#endif

  EXPECT_NE(linux_script.find("'boost'"), std::string::npos);
  EXPECT_NE(linux_script.find("'boost-libs'"), std::string::npos);
  EXPECT_NE(linux_script.find("\"boost-devel\""), std::string::npos);
  EXPECT_NE(linux_script.find("\"boost-static\""), std::string::npos);
  EXPECT_NE(linux_script.find("\"libboost-filesystem-dev\""), std::string::npos);
  EXPECT_NE(linux_script.find("\"libboost-locale-dev\""), std::string::npos);
  EXPECT_NE(linux_script.find("\"libboost-log-dev\""), std::string::npos);
  EXPECT_NE(linux_script.find("\"libboost-program-options-dev\""), std::string::npos);
  EXPECT_NE(linux_script.find("\"libicu-dev\""), std::string::npos);
  EXPECT_NE(linux_script.find("\"26.04\" | sort -V"), std::string::npos);
  EXPECT_NE(linux_script.find("version >= 44"), std::string::npos);
  EXPECT_NE(arch_package.find("'boost'"), std::string::npos);
  EXPECT_NE(arch_package.find("'boost-libs'"), std::string::npos);
#ifdef SUNSHINE_COPR_SPEC_FIXTURE_AVAILABLE
  EXPECT_NE(copr_spec.find("%if 0%{fedora} > 43"), std::string::npos);
  EXPECT_NE(
    copr_spec.find(std::format("BuildRequires: boost-devel >= {}", minimum_version)),
    std::string::npos
  );
  EXPECT_NE(
    copr_spec.find(std::format("BuildRequires: boost-static >= {}", minimum_version)),
    std::string::npos
  );
#endif
#ifdef __APPLE__
  const auto macos_script = read_project_file("scripts/macos_build.sh");
  const auto macos_workflow = read_project_file(".github/workflows/ci-macos.yml");
  EXPECT_NE(macos_script.find("\"boost\""), std::string::npos);
  EXPECT_NE(macos_script.find("LIBRARY_PATH"), std::string::npos);
  EXPECT_NE(macos_workflow.find("boost \\"), std::string::npos);
  EXPECT_NE(macos_workflow.find("LIBRARY_PATH"), std::string::npos);
#endif
  EXPECT_NE(documentation.find("packaged static Boost libraries"), std::string::npos);
}
