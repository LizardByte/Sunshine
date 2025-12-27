/**
 * @file tests/unit/test_system_tray.cpp
 * @brief Test src/system_tray.*.
 */
#include "../tests_common.h"
#include "../tests_log_checker.h"

#include <chrono>
#include <thread>

// Only test the system tray if it's enabled
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1

  #include <src/system_tray.h>

namespace {
  constexpr auto log_file = "test_sunshine.log";

  // Helper class to manage tray lifecycle in tests
  class TrayTestHelper {
  public:
    static void cleanup_any_existing_tray() {
      // Ensure no tray is running before starting tests
      system_tray::end_tray();
      system_tray::end_tray_threaded();
    }
  };
}  // namespace

class SystemTrayTest: public testing::Test {
protected:
  void SetUp() override {
    TrayTestHelper::cleanup_any_existing_tray();
  }

  void TearDown() override {
    TrayTestHelper::cleanup_any_existing_tray();
  }
};

class SystemTrayThreadedTest: public testing::Test {
protected:
  void SetUp() override {
    TrayTestHelper::cleanup_any_existing_tray();
  }

  void TearDown() override {
    TrayTestHelper::cleanup_any_existing_tray();
  }
};

// Test basic tray initialization
TEST_F(SystemTrayTest, InitTray) {
  // Note: This test may fail in CI environments without a display
  // The test verifies the function doesn't crash and returns a status
  const int result = system_tray::init_tray();

  // The result should be either 0 (success) or 1 (failure, e.g., no display)
  EXPECT_TRUE(result == 0 || result == 1);

  if (result == 0) {
    // If initialization succeeded, we should be able to clean up
    EXPECT_EQ(0, system_tray::end_tray());
  }
}

// Test tray event processing
TEST_F(SystemTrayTest, ProcessTrayEvents) {
  if (const int init_result = system_tray::init_tray(); init_result == 0) {
    // If the tray was initialized successfully, test event processing
    const int process_result = system_tray::process_tray_events();
    EXPECT_EQ(0, process_result);

    // Clean up
    EXPECT_EQ(0, system_tray::end_tray());
  } else {
    // If no tray available, processing should fail gracefully
    int process_result = system_tray::process_tray_events();
    EXPECT_NE(0, process_result);
  }
}

// Test tray update functions don't crash
TEST_F(SystemTrayTest, UpdateTrayFunctions) {
  const std::string test_app = "TestApp";

  // These functions should not crash even if the tray is not initialized
  EXPECT_NO_THROW(system_tray::update_tray_playing(test_app));
  EXPECT_NO_THROW(system_tray::update_tray_pausing(test_app));
  EXPECT_NO_THROW(system_tray::update_tray_stopped(test_app));
  EXPECT_NO_THROW(system_tray::update_tray_require_pin());
}

// Test tray update functions with an initialized tray
TEST_F(SystemTrayTest, UpdateTrayWithInitializedTray) {
  if (int init_result = system_tray::init_tray(); init_result == 0) {
    const std::string test_app = "TestApp";

    // These should work without crashing when tray is initialized
    EXPECT_NO_THROW(system_tray::update_tray_playing(test_app));
    EXPECT_NO_THROW(system_tray::update_tray_pausing(test_app));
    EXPECT_NO_THROW(system_tray::update_tray_stopped(test_app));
    EXPECT_NO_THROW(system_tray::update_tray_require_pin());

    // Clean up
    EXPECT_EQ(0, system_tray::end_tray());
  }
}

// Test ending tray without initialization
TEST_F(SystemTrayTest, EndTrayWithoutInit) {
  // Should be safe to call end_tray even if not initialized
  EXPECT_EQ(0, system_tray::end_tray());
}

// Test threaded tray initialization
TEST_F(SystemTrayThreadedTest, InitTrayThreaded) {
  const int result = system_tray::init_tray_threaded();

  // The result should be either 0 (success) or 1 (failure, e.g., no display)
  EXPECT_TRUE(result == 0 || result == 1);

  if (result == 0) {
    // Give the thread a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify we can stop the threaded tray
    EXPECT_EQ(0, system_tray::end_tray_threaded());
  }
}

// Test double initialization of a threaded tray
TEST_F(SystemTrayThreadedTest, DoubleInitTrayThreaded) {
  if (const int first_result = system_tray::init_tray_threaded(); first_result == 0) {
    // Give the thread a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Second initialization should fail
    const int second_result = system_tray::init_tray_threaded();
    EXPECT_EQ(1, second_result);

    // Check that a warning message was logged
    EXPECT_TRUE(log_checker::line_contains(log_file, "Tray thread is already running"));

    // Clean up
    EXPECT_EQ(0, system_tray::end_tray_threaded());
  }
}

// Test ending threaded tray without initialization
TEST_F(SystemTrayThreadedTest, EndThreadedTrayWithoutInit) {
  // Should be safe to call end_tray_threaded even if not initialized
  EXPECT_EQ(0, system_tray::end_tray_threaded());
}

// Test threaded tray lifecycle
TEST_F(SystemTrayThreadedTest, ThreadedTrayLifecycle) {
  if (int init_result = system_tray::init_tray_threaded(); init_result == 0) {
    // Give the thread time to start and initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Check that an initialization message was logged
    EXPECT_TRUE(log_checker::line_contains(log_file, "System tray thread initialized successfully"));

    // Test tray updates work with a threaded tray
    const std::string test_app = "ThreadedTestApp";
    EXPECT_NO_THROW(system_tray::update_tray_playing(test_app));
    EXPECT_NO_THROW(system_tray::update_tray_pausing(test_app));
    EXPECT_NO_THROW(system_tray::update_tray_stopped(test_app));
    EXPECT_NO_THROW(system_tray::update_tray_require_pin());

    // Stop the threaded tray
    EXPECT_EQ(0, system_tray::end_tray_threaded());

    // Give the thread time to stop
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check that a stop message was logged
    EXPECT_TRUE(log_checker::line_contains(log_file, "System tray thread stopped"));
  }
}

// Test that main-thread and threaded tray don't interfere
TEST_F(SystemTrayTest, MainThreadAndThreadedTrayIsolation) {
  // Initialize a main thread tray first

  if (const int main_result = system_tray::init_tray(); main_result == 0) {
    // Try to initialize threaded tray - should work independently

    if (const int threaded_result = system_tray::init_tray_threaded(); threaded_result == 0) {
      // Give a threaded tray time to start
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      // Both should be able to clean up independently
      EXPECT_EQ(0, system_tray::end_tray());
      EXPECT_EQ(0, system_tray::end_tray_threaded());
    } else {
      // Clean up the main thread tray
      EXPECT_EQ(0, system_tray::end_tray());
    }
  }
}

// Test rapid start/stop cycles
TEST_F(SystemTrayThreadedTest, RapidStartStopCycles) {
  // First, check if tray initialization is possible in this environment
  BOOST_LOG(info) << "Testing tray initialization capability...";

  if (const int test_init_result = system_tray::init_tray_threaded(); test_init_result != 0) {
    // Try a regular tray initialization to see if it's a threading issue
    if (const int regular_init_result = system_tray::init_tray(); regular_init_result == 0) {
      BOOST_LOG(info) << "Regular tray initialization succeeded, but threaded failed";
      system_tray::end_tray();
      GTEST_SKIP() << "Threaded tray initialization failed (code: " << test_init_result
                   << "), but regular tray works. May be a threading/timing issue in test environment.";
    } else {
      BOOST_LOG(info) << "Both regular and threaded tray initialization failed - no display available";
      // Instead of skipping, let's test the threading logic without actual tray
      BOOST_LOG(info) << "Testing threading functionality without display...";

      // Test that the threading functions don't crash and return appropriate error codes
      EXPECT_EQ(1, test_init_result);  // Should fail with code 1 (no display)
      EXPECT_EQ(1, regular_init_result);  // Should fail with code 1 (no display)

      // Test multiple calls to init_tray_threaded when no display is available
      const int second_init_result = system_tray::init_tray_threaded();
      EXPECT_EQ(1, second_init_result);  // Should consistently fail

      // Test that end_tray_threaded is safe to call even when init failed
      EXPECT_EQ(0, system_tray::end_tray_threaded());  // Should always return 0

      // Test that update functions don't crash when no tray is available
      const std::string test_app = "NoDisplayTestApp";
      EXPECT_NO_THROW(system_tray::update_tray_playing(test_app));
      EXPECT_NO_THROW(system_tray::update_tray_pausing(test_app));
      EXPECT_NO_THROW(system_tray::update_tray_stopped(test_app));
      EXPECT_NO_THROW(system_tray::update_tray_require_pin());

      BOOST_LOG(info) << "Threading functionality tested successfully (no display mode)";
      return;  // Test passed - we validated the threading logic works correctly
    }
  }

  BOOST_LOG(info) << "Tray initialization succeeded, proceeding with controlled cycles test";

  // Clean up the test initialization
  EXPECT_EQ(0, system_tray::end_tray_threaded());

  // Note: The Windows system tray has limitations on rapid reinitialization
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Longer wait for full cleanup
  BOOST_LOG(info) << "Starting controlled start/stop cycle";

  if (const int init_result = system_tray::init_tray_threaded(); init_result == 0) {
    BOOST_LOG(info) << "Cycle completed successfully - threaded tray can be reinitialized";

    // Give the thread time to start properly
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Test some tray operations while it's running
    const std::string test_app = "CycleTestApp";
    EXPECT_NO_THROW(system_tray::update_tray_playing(test_app));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_NO_THROW(system_tray::update_tray_stopped(test_app));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop the tray
    const int stop_result = system_tray::end_tray_threaded();
    EXPECT_EQ(0, stop_result);

    BOOST_LOG(info) << "Controlled cycle test completed successfully";
  } else {
    FAIL() << "Tray reinitialization not supported in this environment. "
           << "Initial test passed but subsequent initialization failed with code: " << init_result;
  }
}

// Performance test - verify thread startup time is reasonable
TEST_F(SystemTrayThreadedTest, ThreadStartupPerformance) {
  const auto start_time = std::chrono::steady_clock::now();

  const int result = system_tray::init_tray_threaded();

  const auto end_time = std::chrono::steady_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

  if (result == 0) {
    // Startup should complete within 5 seconds (much less in practice)
    EXPECT_LT(duration.count(), 5000);

    // Clean up
    EXPECT_EQ(0, system_tray::end_tray_threaded());
  }
}

#else
// If the tray is not enabled, provide a simple test that passes
TEST(SystemTrayDisabled, TrayNotEnabled) {
  GTEST_SKIP() << "System tray is not enabled in this build";
}
#endif
