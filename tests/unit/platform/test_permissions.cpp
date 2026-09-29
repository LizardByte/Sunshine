/**
 * @file tests/unit/platform/test_permissions.cpp
 * @brief Tests for shared permission policy and directory checks.
 */

// test includes
#include "../../tests_common.h"

// standard includes
#include <chrono>
#include <filesystem>
#include <format>

// platform includes
#ifndef _WIN32
  #include <unistd.h>
#endif

// local includes
#include "src/platform/common.h"
#include "src/platform/permissions.h"
#if defined(__linux__) || defined(__FreeBSD__)
  #include "src/config.h"
#endif

TEST(PlatformPermissionsTest, RequiredPermissionsIgnoreOptionalAndOnUseStatuses) {
  using platf::permission_status_t;
  EXPECT_TRUE(platf::required_permissions_granted({
    permission_status_t {"input", "granted", true, true},
    permission_status_t {"system_audio", "on_use", true, false},
    permission_status_t {"notifications", "denied", false, true},
  }));
  EXPECT_FALSE(platf::required_permissions_granted({
    permission_status_t {"config_directory", "denied", true, true},
  }));
  EXPECT_TRUE(platf::required_permissions_granted({}));
}

TEST(PlatformPermissionsTest, DirectoryAccessRequiresAnExistingDirectory) {
  namespace fs = std::filesystem;
  const auto path = fs::current_path() /
                    std::format("sunshine-permissions-{}", std::chrono::steady_clock::now().time_since_epoch().count());
  ASSERT_TRUE(fs::create_directory(path));
  EXPECT_TRUE(platf::can_access_directory(path));
  EXPECT_FALSE(platf::can_access_directory(path / "missing"));
#ifndef _WIN32
  if (geteuid() != 0) {
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
    EXPECT_FALSE(platf::can_access_directory(path));
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace);
  }
#endif
  fs::remove(path);
  EXPECT_FALSE(platf::can_access_directory(path));
}

#ifdef _WIN32
TEST(PlatformPermissionsTest, WindowsConfigStatusMatchesDirectoryAccess) {
  const auto permissions = platf::get_permission_statuses();
  ASSERT_EQ(permissions.size(), 1U);
  EXPECT_EQ(permissions.front().id, "config_directory");
  EXPECT_EQ(permissions.front().status, platf::can_access_directory(platf::appdata()) ? "granted" : "denied");
  EXPECT_TRUE(permissions.front().required);
  EXPECT_FALSE(permissions.front().requestable);
  EXPECT_FALSE(platf::request_permission("config_directory"));
}
#endif

#if defined(__linux__) || defined(__FreeBSD__)
TEST(PlatformPermissionsTest, UnixInputStatusMatchesVirtualInputDeviceAccess) {
  bool input_access = access("/dev/uinput", R_OK | W_OK) == 0;
  #ifndef __FreeBSD__
  input_access = input_access || access("/dev/input/uinput", R_OK | W_OK) == 0;
  #endif
  const auto permissions = platf::get_permission_statuses();
  ASSERT_EQ(permissions.size(), 1U);
  EXPECT_EQ(permissions.front().id, "input");
  EXPECT_EQ(permissions.front().status, input_access ? "granted" : "denied");
  EXPECT_EQ(permissions.front().required, config::input.keyboard || config::input.mouse || config::input.controller);
  EXPECT_FALSE(permissions.front().requestable);
  EXPECT_FALSE(platf::request_permission("input"));
}
#endif
