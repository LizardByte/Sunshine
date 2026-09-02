/**
 * @file tests/unit/test_stat_trackers.cpp
 * @brief Tests for streaming statistic tracking.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <string>
#include <thread>

// local includes
#include <src/stat_trackers.h>

TEST(StatTrackersFormatTests, OneDigitAfterDecimal) {
  auto fmt = stat_trackers::one_digit_after_decimal();
  const std::string result = (fmt % 12.34).str();
  EXPECT_EQ(result, "12.3");
}

TEST(StatTrackersFormatTests, OneDigitAfterDecimalRoundsUp) {
  auto fmt = stat_trackers::one_digit_after_decimal();
  const std::string result = (fmt % 3.95).str();
  EXPECT_EQ(result, "4.0");
}

TEST(StatTrackersFormatTests, OneDigitAfterDecimalZero) {
  auto fmt = stat_trackers::one_digit_after_decimal();
  const std::string result = (fmt % 0.0).str();
  EXPECT_EQ(result, "0.0");
}

TEST(StatTrackersFormatTests, TwoDigitsAfterDecimal) {
  auto fmt = stat_trackers::two_digits_after_decimal();
  const std::string result = (fmt % 12.34).str();
  EXPECT_EQ(result, "12.34");
}

TEST(StatTrackersFormatTests, TwoDigitsAfterDecimalRoundsUp) {
  auto fmt = stat_trackers::two_digits_after_decimal();
  const std::string result = (fmt % 3.999).str();
  EXPECT_EQ(result, "4.00");
}

TEST(StatTrackersFormatTests, TwoDigitsAfterDecimalZero) {
  auto fmt = stat_trackers::two_digits_after_decimal();
  const std::string result = (fmt % 0.0).str();
  EXPECT_EQ(result, "0.00");
}

TEST(StatTrackersFormatTests, TwoDigitsAfterDecimalNegative) {
  auto fmt = stat_trackers::two_digits_after_decimal();
  const std::string result = (fmt % -1.5).str();
  EXPECT_EQ(result, "-1.50");
}

TEST(StatTrackersMinMaxAvgTests, CallbackNotCalledBeforeInterval) {
  stat_trackers::min_max_avg_tracker<int> tracker;

  bool callback_called = false;
  const auto callback = [&callback_called](int, int, double) {
    callback_called = true;
  };

  tracker.collect_and_callback_on_interval(10, callback, std::chrono::seconds(60));
  EXPECT_FALSE(callback_called);

  tracker.collect_and_callback_on_interval(20, callback, std::chrono::seconds(60));
  EXPECT_FALSE(callback_called);
}

TEST(StatTrackersMinMaxAvgTests, CallbackCalledAfterInterval) {
  stat_trackers::min_max_avg_tracker<int> tracker;

  int result_min = 0;
  int result_max = 0;
  double result_avg = 0;
  bool callback_called = false;

  const auto callback = [&result_min, &result_max, &result_avg, &callback_called](int stat_min, int stat_max, double stat_avg) {
    result_min = stat_min;
    result_max = stat_max;
    result_avg = stat_avg;
    callback_called = true;
  };

  constexpr auto interval = std::chrono::seconds(0);

  tracker.collect_and_callback_on_interval(10, callback, interval);
  EXPECT_FALSE(callback_called);

  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  tracker.collect_and_callback_on_interval(20, callback, interval);
  EXPECT_TRUE(callback_called);

  // The callback reports the completed batch, excluding the triggering sample.
  EXPECT_EQ(result_min, 10);
  EXPECT_EQ(result_max, 10);
  EXPECT_DOUBLE_EQ(result_avg, 10.0);
}

TEST(StatTrackersMinMaxAvgTests, ResetClearsState) {
  stat_trackers::min_max_avg_tracker<int> tracker;

  bool callback_called = false;
  const auto callback = [&callback_called](int, int, double) {
    callback_called = true;
  };

  tracker.collect_and_callback_on_interval(100, callback, std::chrono::seconds(60));
  tracker.reset();

  tracker.collect_and_callback_on_interval(50, callback, std::chrono::seconds(0));
  EXPECT_FALSE(callback_called);
}

TEST(StatTrackersMinMaxAvgTests, MultipleValuesInBatch) {
  stat_trackers::min_max_avg_tracker<int> tracker;

  int result_min = 0;
  int result_max = 0;
  double result_avg = 0;
  bool callback_called = false;

  const auto callback = [&result_min, &result_max, &result_avg, &callback_called](int stat_min, int stat_max, double stat_avg) {
    result_min = stat_min;
    result_max = stat_max;
    result_avg = stat_avg;
    callback_called = true;
  };

  constexpr auto interval = std::chrono::seconds(0);

  tracker.collect_and_callback_on_interval(3, callback, std::chrono::seconds(60));
  tracker.collect_and_callback_on_interval(7, callback, std::chrono::seconds(60));
  tracker.collect_and_callback_on_interval(5, callback, std::chrono::seconds(60));

  EXPECT_FALSE(callback_called);

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  tracker.collect_and_callback_on_interval(100, callback, interval);

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(result_min, 3);
  EXPECT_EQ(result_max, 7);
  EXPECT_DOUBLE_EQ(result_avg, 5.0);
}

TEST(StatTrackersMinMaxAvgTests, TracksNegativeDoubleValues) {
  stat_trackers::min_max_avg_tracker<double> tracker;

  double result_min = 0;
  double result_max = 0;
  double result_avg = 0;
  bool callback_called = false;

  const auto callback = [&result_min, &result_max, &result_avg, &callback_called](double stat_min, double stat_max, double stat_avg) {
    result_min = stat_min;
    result_max = stat_max;
    result_avg = stat_avg;
    callback_called = true;
  };

  tracker.collect_and_callback_on_interval(-3.5, callback, std::chrono::seconds(60));
  tracker.collect_and_callback_on_interval(-2.5, callback, std::chrono::seconds(60));
  tracker.collect_and_callback_on_interval(-1.5, callback, std::chrono::seconds(60));

  EXPECT_FALSE(callback_called);

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  tracker.collect_and_callback_on_interval(0.0, callback, std::chrono::seconds(0));

  EXPECT_TRUE(callback_called);
  EXPECT_DOUBLE_EQ(result_min, -3.5);
  EXPECT_DOUBLE_EQ(result_max, -1.5);
  EXPECT_DOUBLE_EQ(result_avg, -2.5);
}
