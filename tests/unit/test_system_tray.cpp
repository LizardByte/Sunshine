/**
 * @file tests/unit/test_system_tray.cpp
 * @brief Tests for Sunshine's system tray integration.
 */
#include "../tests_common.h"

// standard includes
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <tuple>

// Only test the system tray if it is enabled.
#if defined(SUNSHINE_TRAY) && SUNSHINE_TRAY >= 1

  // lib includes
  #include <tray.h>

  // local includes
  #include <src/system_tray.h>

  #if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    // lib includes
    #include <lizardbyte/common/env.h>

    // test includes
    #include "third-party/tray/tests/notification_utils.h"
    #include "third-party/tray/tests/screenshot_utils.h"
  #endif

namespace {
  using namespace std::chrono_literals;

  #ifndef _WIN32
  /**
   * @brief Process pending Qt tray events without blocking.
   *
   * @param iterations Maximum number of event iterations to process.
   * @param delay Delay between event iterations.
   */
  void pump_tray_events(const int iterations = 100, const std::chrono::milliseconds delay = 5ms) {
    for (int i = 0; i < iterations; ++i) {
      if (tray_loop(0) != 0) {
        return;
      }
      std::this_thread::sleep_for(delay);
    }
  }
  #else

  /**
   * @brief Wait for the threaded tray worker to finish initialization.
   *
   * @param timeout Maximum time to wait.
   * @return true if the tray initialized before the timeout; otherwise, false.
   */
  bool wait_for_tray_initialization(const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (system_tray::tray_initialized_for_testing()) {
        return true;
      }
      std::this_thread::sleep_for(10ms);
    }
    return false;
  }
  #endif

  /**
   * @brief Verify the persistent menu exposed by Sunshine.
   */
  void verify_menu() {
    const auto &tray_data = system_tray::tray_data_for_testing();
    ASSERT_NE(tray_data.menu, nullptr);

    EXPECT_STREQ(tray_data.menu[0].text, "Open Sunshine");
    EXPECT_NE(tray_data.menu[0].cb, nullptr);
    EXPECT_STREQ(tray_data.menu[1].text, "-");
    EXPECT_EQ(tray_data.menu[1].cb, nullptr);
    EXPECT_STREQ(tray_data.menu[2].text, "Donate");
    ASSERT_NE(tray_data.menu[2].submenu, nullptr);
    EXPECT_STREQ(tray_data.menu[2].submenu[0].text, "GitHub Sponsors");
    EXPECT_STREQ(tray_data.menu[2].submenu[1].text, "Patreon");
    EXPECT_STREQ(tray_data.menu[2].submenu[2].text, "PayPal");
    EXPECT_EQ(tray_data.menu[2].submenu[3].text, nullptr);
    EXPECT_STREQ(tray_data.menu[3].text, "-");

  #ifdef _WIN32
    EXPECT_STREQ(tray_data.menu[4].text, "Reset Display Device Config");
    EXPECT_NE(tray_data.menu[4].cb, nullptr);
    EXPECT_STREQ(tray_data.menu[5].text, "Restart");
    EXPECT_NE(tray_data.menu[5].cb, nullptr);
    EXPECT_STREQ(tray_data.menu[6].text, "Quit");
    EXPECT_NE(tray_data.menu[6].cb, nullptr);
    EXPECT_EQ(tray_data.menu[7].text, nullptr);
  #else
    EXPECT_STREQ(tray_data.menu[4].text, "Restart");
    EXPECT_NE(tray_data.menu[4].cb, nullptr);
    EXPECT_STREQ(tray_data.menu[5].text, "Quit");
    EXPECT_NE(tray_data.menu[5].cb, nullptr);
    EXPECT_EQ(tray_data.menu[6].text, nullptr);
  #endif
  }

  /**
   * @brief Verify the current user-visible tray state.
   *
   * @param icon_index Index of the expected tray icon.
   * @param tooltip Expected tray tooltip.
   * @param notification_title Expected notification title, or nullptr when none is expected.
   * @param notification_text Expected notification text, or nullptr when none is expected.
   * @param notification_icon_index Index of the expected notification icon, or no value when none is expected.
   * @param expects_notification_callback Whether a notification callback is expected.
   */
  void verify_state(
    const std::size_t icon_index,
    const char *tooltip,
    const char *notification_title,
    const char *notification_text,
    const std::optional<std::size_t> notification_icon_index,
    const bool expects_notification_callback
  ) {
    const auto &tray_data = system_tray::tray_data_for_testing();
    ASSERT_GE(tray_data.iconPathCount, 4);

    EXPECT_STREQ(tray_data.icon, tray_data.allIconPaths[icon_index]);
    EXPECT_STREQ(tray_data.tooltip, tooltip);
    EXPECT_STREQ(tray_data.notification_title, notification_title);
    EXPECT_STREQ(tray_data.notification_text, notification_text);
    if (notification_icon_index.has_value()) {
      EXPECT_STREQ(tray_data.notification_icon, tray_data.allIconPaths[*notification_icon_index]);
    } else {
      EXPECT_EQ(tray_data.notification_icon, nullptr);
    }
    if (expects_notification_callback) {
      EXPECT_NE(tray_data.notification_cb, nullptr);
    } else {
      EXPECT_EQ(tray_data.notification_cb, nullptr);
    }
  }

  /**
   * @brief Verify every user-visible tray state transition.
   */
  #ifndef _WIN32
  void verify_state_transitions() {
    verify_state(0, PROJECT_NAME, nullptr, nullptr, std::nullopt, false);

    system_tray::update_tray_playing("Moonlight");
    verify_state(2, "Streaming started for Moonlight", "Stream Started", "Streaming started for Moonlight", 2, false);

    system_tray::update_tray_pausing("Moonlight");
    verify_state(3, "Streaming paused for Moonlight", "Stream Paused", "Streaming paused for Moonlight", 3, false);

    system_tray::update_tray_stopped("Moonlight");
    verify_state(0, PROJECT_NAME, "Application Stopped", "Application Moonlight successfully stopped", 0, false);

    system_tray::update_tray_require_pin();
    verify_state(0, PROJECT_NAME, "Incoming Pairing Request", "Click here to complete the pairing process", 1, true);
  }
  #endif
}  // namespace

/**
 * @brief Fixture that resets the process-wide tray state between tests.
 */
class SystemTrayTest: public testing::Test {
protected:
  /**
   * @brief Reset any tray state left by an earlier test.
   */
  void SetUp() override {
    EXPECT_EQ(system_tray::end_tray(), 0);
  #ifndef _WIN32
    std::ignore = tray_loop(0);
    std::ignore = tray_restore_mouse_position();
  #endif
    system_tray::reset_tray_data_for_testing();
  }

  /**
   * @brief Shut down and reset the tray after a test.
   */
  void TearDown() override {
  #if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
    dismissNativeNotifications();
  #endif
    EXPECT_EQ(system_tray::end_tray(), 0);
  #ifdef _WIN32
    std::this_thread::sleep_for(250ms);
  #endif
  #ifndef _WIN32
    std::ignore = tray_loop(0);
    std::ignore = tray_restore_mouse_position();
  #endif
    system_tray::reset_tray_data_for_testing();
  }

  /**
   * @brief Initialize the tray and process its initial events.
   *
   * @return The result returned by system_tray::init_tray().
   */
  int initialize_tray() const {
    const int result = system_tray::init_tray();
  #ifndef _WIN32
    if (result == 0) {
      pump_tray_events();
    }
  #endif
    return result;
  }
};

TEST_F(SystemTrayTest, UpdatesAreIgnoredBeforeInitialization) {
  EXPECT_FALSE(system_tray::tray_initialized_for_testing());
  EXPECT_EQ(system_tray::process_tray_events(), 1);

  system_tray::update_tray_playing("Moonlight");
  system_tray::update_tray_pausing("Moonlight");
  system_tray::update_tray_stopped("Moonlight");
  system_tray::update_tray_require_pin();

  const auto &tray_data = system_tray::tray_data_for_testing();
  EXPECT_STREQ(tray_data.icon, tray_data.allIconPaths[0]);
  EXPECT_STREQ(tray_data.tooltip, PROJECT_NAME);
  EXPECT_EQ(tray_data.notification_title, nullptr);
  EXPECT_EQ(tray_data.notification_text, nullptr);
  EXPECT_EQ(tray_data.notification_icon, nullptr);
  EXPECT_EQ(tray_data.notification_cb, nullptr);
  EXPECT_EQ(system_tray::end_tray(), 0);
}

  #ifndef _WIN32
TEST_F(SystemTrayTest, LifecycleMenuAndStateTransitions) {
  if (const int result = initialize_tray(); result != 0) {
    GTEST_SKIP() << "System tray is unavailable in this environment (code " << result << ")";
  }

  EXPECT_TRUE(system_tray::tray_initialized_for_testing());
  verify_menu();
  verify_state_transitions();

  std::jthread exit_thread([]() {
    std::this_thread::sleep_for(100ms);
    std::ignore = system_tray::end_tray();
  });
  EXPECT_NE(system_tray::process_tray_events(), 0);
  exit_thread.join();
  EXPECT_FALSE(system_tray::tray_initialized_for_testing());
}
  #endif

  #ifdef _WIN32
TEST_F(SystemTrayTest, InitializesTrayForWorkflowConfiguration) {
  if (std::getenv("SUNSHINE_CONFIGURE_TRAY_ICONS") == nullptr) {
    GTEST_SKIP() << "Only required while configuring Windows runner tray icon visibility";
  }

  ASSERT_EQ(system_tray::init_tray_threaded(), 0);
  ASSERT_TRUE(wait_for_tray_initialization(10s));
  verify_menu();
  EXPECT_EQ(system_tray::end_tray(), 0);
}
  #endif

  #if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
/**
 * @brief Fixture that stores visual tray evidence in the test output directory.
 */
class SystemTrayVisualTest: public SystemTrayTest {
protected:
  /**
   * @brief Initialize a clean screenshot output directory for this test suite.
   */
  static void SetUpTestSuite() {
    const std::filesystem::path test_binary_dir {SUNSHINE_TEST_BIN_DIR};
    std::error_code remove_error;
    std::filesystem::remove_all(test_binary_dir / "screenshots", remove_error);
    screenshot::initialize(test_binary_dir);
  }

  /**
   * @brief Wait for a tray notification to be rendered.
   */
  static void wait_for_notification() {
    #ifndef _WIN32
    pump_tray_events();
    #endif
    #if defined(_WIN32) || defined(__APPLE__)
    if (lizardbyte::common::is_github_actions()) {
      #ifdef _WIN32
      std::this_thread::sleep_for(2s);
      #else
      pump_tray_events(40, 50ms);
      #endif
    }
    #endif
    std::this_thread::sleep_for(500ms);
  }

  /**
   * @brief Capture a screenshot and dismiss its native notification.
   *
   * @param name Screenshot filename without the PNG extension.
   */
  static void capture_notification(const std::string &name) {
    wait_for_notification();
    EXPECT_TRUE(screenshot::capture(name));
    waitForNativeNotificationTimeout();
  }

  /**
   * @brief Initialize the tray on the platform's production execution path.
   *
   * @return Zero when initialization succeeds; otherwise, a non-zero error code.
   */
  int initialize_visual_tray() const {
    #ifdef _WIN32
    if (const int result = system_tray::init_tray_threaded(); result != 0) {
      return result;
    }
    return wait_for_tray_initialization(10s) ? 0 : 1;
    #else
    return initialize_tray();
    #endif
  }
};

TEST_F(SystemTrayVisualTest, CapturesIconTooltipNotificationsAndMenu) {
  std::string unavailable_reason;
  if (!screenshot::is_available(&unavailable_reason)) {
    GTEST_SKIP() << "Screenshot tooling is unavailable: " << unavailable_reason;
  }
  if (const int result = initialize_visual_tray(); result != 0) {
    GTEST_SKIP() << "System tray is unavailable in this environment (code " << result << ")";
  }

  verify_menu();
  verify_state(0, PROJECT_NAME, nullptr, nullptr, std::nullopt, false);
  EXPECT_TRUE(screenshot::capture("sunshine_tray_initial"));

  const int tooltip_position_result = tray_position_mouse_over_icon();
  if (lizardbyte::common::is_github_actions()) {
    ASSERT_EQ(tooltip_position_result, 0);
  }
  if (tooltip_position_result == 0) {
    #ifdef _WIN32
    std::this_thread::sleep_for(1s);
    #else
    pump_tray_events(20, 50ms);
    #endif
    EXPECT_TRUE(screenshot::capture("sunshine_tray_tooltip"));
    EXPECT_EQ(tray_restore_mouse_position(), 0);
  }

  dismissNativeNotifications();
  system_tray::update_tray_playing("Moonlight");
  verify_state(2, "Streaming started for Moonlight", "Stream Started", "Streaming started for Moonlight", 2, false);
  capture_notification("sunshine_tray_streaming");
  system_tray::update_tray_pausing("Moonlight");
  verify_state(3, "Streaming paused for Moonlight", "Stream Paused", "Streaming paused for Moonlight", 3, false);
  capture_notification("sunshine_tray_paused");
  system_tray::update_tray_stopped("Moonlight");
  verify_state(0, PROJECT_NAME, "Application Stopped", "Application Moonlight successfully stopped", 0, false);
  capture_notification("sunshine_tray_stopped");
  system_tray::update_tray_require_pin();
  verify_state(0, PROJECT_NAME, "Incoming Pairing Request", "Click here to complete the pairing process", 1, true);
  capture_notification("sunshine_tray_pairing_request");

  int menu_position_result = -1;
  if (lizardbyte::common::is_github_actions()) {
    #ifdef _WIN32
    std::this_thread::sleep_for(500ms);
    #else
    pump_tray_events();
    #endif
    menu_position_result = tray_position_mouse_over_icon();
    ASSERT_EQ(menu_position_result, 0);
  }

  std::atomic_bool capture_result {false};
  std::jthread capture_thread([&capture_result]() {
    capture_result.store(screenshot::capture("sunshine_tray_menu"), std::memory_order_release);
    std::ignore = system_tray::end_tray();
  });

  tray_show_menu();
  if (menu_position_result == 0) {
    EXPECT_EQ(tray_restore_mouse_position(), 0);
  }
    #ifdef _WIN32
  capture_thread.join();
    #else
  EXPECT_NE(system_tray::process_tray_events(), 0);
  capture_thread.join();
    #endif
  EXPECT_TRUE(capture_result.load(std::memory_order_acquire));
}
  #endif

#else

TEST(SystemTrayDisabled, ReportsDisabledBuild) {
  GTEST_SKIP() << "System tray support is disabled in this build";
}

#endif
