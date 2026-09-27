/**
 * @file tests/unit/platform/macos/test_permissions.cpp
 * @brief Tests for macOS startup permission policy.
 */

#ifdef __APPLE__
  // test includes
  #include "../../../tests_common.h"

  // local includes
  #include "src/platform/macos/misc.h"

TEST(MacosPermissionsTest, RequiredPermissionsIgnoreOptionalAndOnUseStatuses) {
  using platf::permission_status_t;
  EXPECT_TRUE(platf::required_permissions_granted({
    permission_status_t {"screen_recording", "granted", true, true},
    permission_status_t {"system_audio", "on_use", true, false},
    permission_status_t {"local_network", "on_use", true, false},
    permission_status_t {"notifications", "denied", false, true},
  }));
  EXPECT_FALSE(platf::required_permissions_granted({
    permission_status_t {"screen_recording", "denied", true, true},
    permission_status_t {"input", "granted", true, true},
  }));
  EXPECT_FALSE(platf::required_permissions_granted({
    permission_status_t {"microphone", "not_determined", true, true},
  }));
  EXPECT_TRUE(platf::required_permissions_granted({
    permission_status_t {"microphone", "not_determined", false, true},
  }));
}

TEST(MacosPermissionsTest, StartupRequestsOnlyMissingPermissionsInUse) {
  using platf::permission_status_t;
  EXPECT_TRUE(platf::should_request_startup_permission({"screen_recording", "denied", true, true}, false));
  EXPECT_TRUE(platf::should_request_startup_permission({"input", "not_determined", true, true}, false));
  EXPECT_TRUE(platf::should_request_startup_permission({"microphone", "not_determined", true, true}, false));
  EXPECT_TRUE(platf::should_request_startup_permission({"notifications", "not_determined", false, true}, true));
  EXPECT_FALSE(platf::should_request_startup_permission({"screen_recording", "granted", true, true}, true));
  EXPECT_FALSE(platf::should_request_startup_permission({"input", "denied", false, true}, true));
  EXPECT_TRUE(platf::should_request_startup_permission({"microphone", "denied", true, true}, true));
  EXPECT_FALSE(platf::should_request_startup_permission({"microphone", "denied", false, true}, true));
  EXPECT_FALSE(platf::should_request_startup_permission({"notifications", "not_determined", false, true}, false));
  EXPECT_FALSE(platf::should_request_startup_permission({"system_audio", "on_use", true, false}, true));
}
#endif
