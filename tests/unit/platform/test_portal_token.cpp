/**
 * @file tests/unit/platform/test_portal_token.cpp
 * @brief Tests for session-specific XDG Portal token paths and legacy migration.
 */

// test includes
#include "../../tests_common.h"

#ifndef SUNSHINE_BUILD_PORTAL
TEST_F(BaseTest, XdgRestoreTokenPathUsesLegacyFilenameWithoutPortal) {
  EXPECT_EQ(platf::get_xdg_restore_token_path(), platf::appdata() / "portal_token");
}
#else
  // standard includes
  #include <cstdlib>
  #include <filesystem>
  #include <fstream>
  #include <string>

  // local includes
  #include "src/platform/linux/misc.h"

/**
 * @brief Desktop input and expected normalized token suffix.
 */
struct portal_desktop_suffix_t {
  const char *desktop;  ///< Desktop identifier supplied to the resolver.
  const char *suffix;  ///< Expected safe filename suffix.
};

namespace {
  /**
   * @brief Provide a private token directory without changing process environment variables.
   */
  class PortalTokenTest: public BaseTest {
  protected:
    /**
     * @brief Create a unique writable directory for test tokens.
     */
    void SetUp() override {
      BaseTest::SetUp();
      if (HasFatalFailure() || IsSkipped()) {
        return;
      }
      auto directory_template = (std::filesystem::temp_directory_path() / "sunshine-portal-token-XXXXXX").string();  // NOSONAR(cpp:S5443): mkdtemp creates a private unique directory.
      const auto *directory = mkdtemp(directory_template.data());
      ASSERT_NE(directory, nullptr);
      token_directory_ = directory;
    }

    /**
     * @brief Remove only the private directory created by this fixture.
     */
    void TearDown() override {
      if (!token_directory_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(token_directory_, ec);
        EXPECT_FALSE(ec) << ec.message();
      }
      BaseTest::TearDown();
    }

    /**
     * @brief Resolve a token path through the production resolver.
     * @param desktop Desktop identifier to normalize and validate.
     * @return Selected path in the private token directory.
     */
    std::filesystem::path resolve(const std::string &desktop = "KDE") const {
      return portal::get_saved_token_path_for_testing(token_directory_, desktop);
    }

    /**
     * @brief Write a token in the private directory.
     * @param name Token filename.
     * @param value Token contents.
     */
    void write_token(const std::string &name, const std::string &value) const {
      std::ofstream file(token_directory_ / name);
      ASSERT_TRUE(file.is_open());
      file << value;
      ASSERT_TRUE(file.good());
    }

    /**
     * @brief Read a token without changing its contents.
     * @param name Token filename.
     * @return First line of the saved token.
     */
    std::string read_token(const std::string &name) const {
      std::ifstream file(token_directory_ / name);
      std::string value;
      std::getline(file, value);
      return value;
    }

    /**
     * @brief Get the private directory used by token assertions.
     *
     * @return Read-only path to this fixture's token directory.
     */
    const std::filesystem::path &token_directory() const {
      return token_directory_;
    }

  private:
    std::filesystem::path token_directory_;  ///< Private directory owned by this fixture.
  };

  /**
   * @brief Parameterized suffix normalization and validation tests.
   */
  class PortalTokenSuffixTest: public PortalTokenTest, public testing::WithParamInterface<portal_desktop_suffix_t> {};
}  // namespace

TEST_P(PortalTokenSuffixTest, SelectsSafeSessionFilename) {
  const auto &value = GetParam();
  EXPECT_EQ(resolve(value.desktop), token_directory() / (std::string("portal_token.") + value.suffix));
}

INSTANTIATE_TEST_SUITE_P(
  DesktopNames,
  PortalTokenSuffixTest,
  testing::Values(
    portal_desktop_suffix_t {"KDE", "kde"},
    portal_desktop_suffix_t {"GNOME", "gnome"},
    portal_desktop_suffix_t {"Hyprland", "hyprland"},
    portal_desktop_suffix_t {"Desktop_1-test", "desktop_1-test"},
    portal_desktop_suffix_t {"", "unknown"},
    portal_desktop_suffix_t {"../kde", "unknown"},
    portal_desktop_suffix_t {"kde/gnome", "unknown"},
    portal_desktop_suffix_t {"kde\\gnome", "unknown"},
    portal_desktop_suffix_t {"kde:gnome", "unknown"},
    portal_desktop_suffix_t {"kde.name", "unknown"}
  )
);

TEST_F(PortalTokenTest, MigratesLegacyTokenAndPreservesContents) {
  ASSERT_NO_FATAL_FAILURE(write_token("portal_token", "legacy-token"));
  EXPECT_EQ(resolve(), token_directory() / "portal_token.kde");
  EXPECT_FALSE(std::filesystem::exists(token_directory() / "portal_token"));
  EXPECT_EQ(read_token("portal_token.kde"), "legacy-token");
  EXPECT_EQ(resolve(), token_directory() / "portal_token.kde");
  EXPECT_EQ(read_token("portal_token.kde"), "legacy-token");
}

TEST_F(PortalTokenTest, MigratesLegacyTokenToUnknownWhenDesktopIsEmpty) {
  ASSERT_NO_FATAL_FAILURE(write_token("portal_token", "legacy-token"));
  EXPECT_EQ(resolve(""), token_directory() / "portal_token.unknown");
  EXPECT_FALSE(std::filesystem::exists(token_directory() / "portal_token"));
  EXPECT_EQ(read_token("portal_token.unknown"), "legacy-token");
}

TEST_F(PortalTokenTest, ExistingSessionTokenPreventsLegacyMigration) {
  ASSERT_NO_FATAL_FAILURE(write_token("portal_token", "legacy-token"));
  ASSERT_NO_FATAL_FAILURE(write_token("portal_token.kde", "current-token"));
  EXPECT_EQ(resolve(), token_directory() / "portal_token.kde");
  EXPECT_EQ(read_token("portal_token"), "legacy-token");
  EXPECT_EQ(read_token("portal_token.kde"), "current-token");
}

TEST_F(PortalTokenTest, MigrationPreservesOtherDesktopToken) {
  ASSERT_NO_FATAL_FAILURE(write_token("portal_token", "legacy-token"));
  ASSERT_NO_FATAL_FAILURE(write_token("portal_token.gnome", "gnome-token"));
  EXPECT_EQ(resolve(), token_directory() / "portal_token.kde");
  EXPECT_EQ(read_token("portal_token.kde"), "legacy-token");
  EXPECT_EQ(read_token("portal_token.gnome"), "gnome-token");
}

TEST_F(PortalTokenTest, SessionPathLookupErrorLeavesLegacyTokenUntouched) {
  ASSERT_NO_FATAL_FAILURE(write_token("portal_token", "legacy-token"));
  std::filesystem::create_symlink("portal_token.kde", token_directory() / "portal_token.kde");
  EXPECT_EQ(resolve(), token_directory() / "portal_token.kde");
  EXPECT_TRUE(std::filesystem::is_symlink(token_directory() / "portal_token.kde"));
  EXPECT_EQ(read_token("portal_token"), "legacy-token");
}

TEST_F(PortalTokenTest, LegacyPathLookupErrorDoesNotCreateSessionToken) {
  std::filesystem::create_symlink("portal_token", token_directory() / "portal_token");
  EXPECT_EQ(resolve(), token_directory() / "portal_token.kde");
  EXPECT_TRUE(std::filesystem::is_symlink(token_directory() / "portal_token"));
  EXPECT_FALSE(std::filesystem::exists(token_directory() / "portal_token.kde"));
}

TEST_F(PortalTokenTest, FailedRenamePreservesLegacyEntryAndSessionPath) {
  ASSERT_TRUE(std::filesystem::create_directory(token_directory() / "portal_token"));
  std::filesystem::create_symlink("missing-token", token_directory() / "portal_token.kde");
  // POSIX cannot rename a directory over a symlink, even when its target is missing.
  EXPECT_EQ(resolve(), token_directory() / "portal_token.kde");
  EXPECT_TRUE(std::filesystem::is_directory(token_directory() / "portal_token"));
  EXPECT_TRUE(std::filesystem::is_symlink(token_directory() / "portal_token.kde"));
}
#endif
