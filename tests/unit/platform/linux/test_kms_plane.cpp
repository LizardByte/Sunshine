/**
 * @file tests/unit/platform/linux/test_kms_plane.cpp
 * @brief Tests for tracking the DRM plane captured by KMS capture.
 */

#ifdef __linux__

  // standard includes
  #include <chrono>

  // lib includes
  #include <gtest/gtest.h>

  // local includes
  #include "src/platform/linux/kms_plane.h"

using namespace std::literals;

namespace {
  const auto start = std::chrono::steady_clock::time_point {} + 1h;  ///< An arbitrary starting time.
}  // namespace

TEST(KmsEmptyPlaneTimerTest, NeverDueWhilePlaneHasFramebuffer) {
  platf::kms::empty_plane_timer_t timer {250ms};
  EXPECT_FALSE(timer.update(true, start));
  EXPECT_FALSE(timer.update(true, start + 10s));
  EXPECT_EQ(timer.empty_for(start + 10s), 0ms);
}

TEST(KmsEmptyPlaneTimerTest, DueOnceEmptyForTheDelay) {
  platf::kms::empty_plane_timer_t timer {250ms};
  EXPECT_FALSE(timer.update(false, start));
  EXPECT_FALSE(timer.update(false, start + 249ms));
  EXPECT_TRUE(timer.update(false, start + 250ms));
  EXPECT_EQ(timer.empty_for(start + 300ms), 300ms);
}

TEST(KmsEmptyPlaneTimerTest, FramebufferReturningResetsTheDelay) {
  platf::kms::empty_plane_timer_t timer {250ms};
  EXPECT_FALSE(timer.update(false, start));
  EXPECT_FALSE(timer.update(true, start + 200ms));
  EXPECT_FALSE(timer.update(false, start + 300ms));
  EXPECT_FALSE(timer.update(false, start + 500ms));
  EXPECT_TRUE(timer.update(false, start + 550ms));
}

#endif
