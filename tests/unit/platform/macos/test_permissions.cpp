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

TEST(MacosPermissionsTest, CoreGraphicsPromptOnlyOnFirstRequest) {
  using platf::permission_request_action_t;
  EXPECT_EQ(platf::permission_request_action(true, 0, true, false), permission_request_action_t::none);
  EXPECT_EQ(platf::permission_request_action(true, 1, false, true), permission_request_action_t::none);
  EXPECT_EQ(platf::permission_request_action(false, 0, true, false), permission_request_action_t::prompt);
  EXPECT_EQ(platf::permission_request_action(false, 1, true, true), permission_request_action_t::none);
  EXPECT_EQ(platf::permission_request_action(false, 1, false, false), permission_request_action_t::settings);
  EXPECT_EQ(platf::permission_request_action(false, 1, false, true), permission_request_action_t::prompt);
  EXPECT_EQ(platf::permission_request_action(false, 2, true, false), permission_request_action_t::settings);
}

TEST(MacosPermissionsTest, LocalNetworkPrivacyStartsWithMacOS15) {
  EXPECT_FALSE(platf::supports_local_network_privacy(14));
  EXPECT_TRUE(platf::supports_local_network_privacy(15));
  EXPECT_TRUE(platf::supports_local_network_privacy(27));
}

TEST(MacosPermissionsTest, StartupSystemAudioProbeRunsOnlyOnce) {
  EXPECT_TRUE(platf::should_request_startup_system_audio_permission(false, false));
  EXPECT_FALSE(platf::should_request_startup_system_audio_permission(false, true));
  EXPECT_FALSE(platf::should_request_startup_system_audio_permission(true, false));
  EXPECT_FALSE(platf::should_request_startup_system_audio_permission(true, true));
}
#endif
