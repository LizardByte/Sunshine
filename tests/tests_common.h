/**
 * @file tests/tests_common.h
 * @brief Common declarations.
 */
#pragma once
#include <gtest/gtest.h>
#include <src/globals.h>
#include <src/logging.h>
#include <src/platform/common.h>

// XFail/XPass pattern implementation (similar to pytest)
namespace test_utils {
  /**
   * @brief Marks a test as expected to fail
   * @param condition The condition under which the test is expected to fail
   * @param reason The reason why the test is expected to fail
   */
  struct XFailMarker {
    bool should_xfail;
    std::string reason;

    XFailMarker(bool condition, std::string reason):
        should_xfail(condition),
        reason(std::move(reason)) {}
  };

  /**
   * @brief Helper function to handle xfail logic
   * @param marker The XFailMarker containing condition and reason
   * @param test_passed Whether the test actually passed
   */
  inline void handleXFail(const XFailMarker &marker, bool test_passed) {
    if (marker.should_xfail) {
      if (test_passed) {
        // XPass: Test was expected to fail but passed
        const std::string message = "XPASS: Test unexpectedly passed (expected to fail: " + marker.reason + ")";
        BOOST_LOG(warning) << message;
        GTEST_SKIP() << "XPASS: Test unexpectedly passed (expected to fail: " << marker.reason << ")";
      }
      // XFail: Test failed as expected
      const std::string message = "XFAIL: Test failed as expected (" + marker.reason + ")";
      BOOST_LOG(info) << message;
      GTEST_SKIP() << "XFAIL: " << marker.reason;
    }
    // If not marked as xfail, let the test result stand as normal
  }
}  // namespace test_utils

// Convenience macros for xfail testing
#define XFAIL_IF(condition, reason) \
  test_utils::XFailMarker xfail_marker((condition), (reason))

#define HANDLE_XFAIL(test_expression) \
  do { \
    bool test_result = false; \
    try { \
      test_expression; \
      test_result = true; \
    } catch (...) { \
      test_result = false; \
    } \
    test_utils::handleXFail(xfail_marker, test_result); \
    if (!xfail_marker.should_xfail) { \
      test_expression; /* Re-run the test normally if not xfail */ \
    } \
  } while (0)

// Platform detection macros for convenience
#ifdef _WIN32
  #define IS_WINDOWS true
#else
  #define IS_WINDOWS false
#endif

#ifdef __linux__
  #define IS_LINUX true
#else
  #define IS_LINUX false
#endif

#ifdef __APPLE__
  #define IS_MACOS true
#else
  #define IS_MACOS false
#endif

struct PlatformTestSuite: testing::Test {
  static void SetUpTestSuite() {
    ASSERT_FALSE(platf_deinit);
    BOOST_LOG(tests) << "Setting up platform test suite";
    platf_deinit = platf::init();
    ASSERT_TRUE(platf_deinit);
  }

  static void TearDownTestSuite() {
    ASSERT_TRUE(platf_deinit);
    platf_deinit = {};
    BOOST_LOG(tests) << "Tore down platform test suite";
  }

private:
  inline static std::unique_ptr<platf::deinit_t> platf_deinit;
};
