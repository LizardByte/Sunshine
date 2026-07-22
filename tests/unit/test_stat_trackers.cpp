/**
 * @file tests/unit/test_stat_trackers.cpp
 * @brief Test src/stat_trackers.h and src/stat_trackers.cpp.
 */
#include "../tests_common.h"

#include <src/stat_trackers.h>

#include <thread>

// ========== Format helper tests ==========

TEST(StatTrackersFormatTests, OneDigitAfterDecimal) {
  auto fmt = stat_trackers::one_digit_after_decimal();
  std::string result = (fmt % 3.14159).str();
  EXPECT_EQ(result, "3.1");
}

TEST(StatTrackersFormatTests, OneDigitAfterDecimalRoundsUp) {
  auto fmt = stat_trackers::one_digit_after_decimal();
  std::string result = (fmt % 3.95).str();
  EXPECT_EQ(result, "4.0");
}

TEST(StatTrackersFormatTests, OneDigitAfterDecimalZero) {
  auto fmt = stat_trackers::one_digit_after_decimal();
  std::string result = (fmt % 0.0).str();
  EXPECT_EQ(result, "0.0");
}

TEST(StatTrackersFormatTests, TwoDigitsAfterDecimal) {
  auto fmt = stat_trackers::two_digits_after_decimal();
  std::string result = (fmt % 3.14159).str();
  EXPECT_EQ(result, "3.14");
}

TEST(StatTrackersFormatTests, TwoDigitsAfterDecimalRoundsUp) {
  auto fmt = stat_trackers::two_digits_after_decimal();
  std::string result = (fmt % 3.999).str();
  EXPECT_EQ(result, "4.00");
}

TEST(StatTrackersFormatTests, TwoDigitsAfterDecimalZero) {
  auto fmt = stat_trackers::two_digits_after_decimal();
  std::string result = (fmt % 0.0).str();
  EXPECT_EQ(result, "0.00");
}

TEST(StatTrackersFormatTests, TwoDigitsAfterDecimalNegative) {
  auto fmt = stat_trackers::two_digits_after_decimal();
  std::string result = (fmt % -1.5).str();
  EXPECT_EQ(result, "-1.50");
}

// ========== min_max_avg_tracker tests ==========

TEST(StatTrackersMinMaxAvgTests, CallbackNotCalledBeforeInterval) {
  stat_trackers::min_max_avg_tracker<int> tracker;

  bool callback_called = false;
  auto callback = [&](int, int, double) {
    callback_called = true;
  };

  // First call initializes the timer
  tracker.collect_and_callback_on_interval(10, callback, std::chrono::seconds(60));
  EXPECT_FALSE(callback_called);

  // Second call within interval should not trigger callback
  tracker.collect_and_callback_on_interval(20, callback, std::chrono::seconds(60));
  EXPECT_FALSE(callback_called);
}

TEST(StatTrackersMinMaxAvgTests, CallbackCalledAfterInterval) {
  stat_trackers::min_max_avg_tracker<int> tracker;

  int result_min = 0;
  int result_max = 0;
  double result_avg = 0;
  bool callback_called = false;

  auto callback = [&](int stat_min, int stat_max, double stat_avg) {
    result_min = stat_min;
    result_max = stat_max;
    result_avg = stat_avg;
    callback_called = true;
  };

  // Use a very short interval for testing
  auto interval = std::chrono::seconds(0);

  // First call sets the timer
  tracker.collect_and_callback_on_interval(10, callback, interval);
  EXPECT_FALSE(callback_called);

  // Wait a tiny bit so time passes
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // Second call should trigger callback since interval has passed
  tracker.collect_and_callback_on_interval(20, callback, interval);
  EXPECT_TRUE(callback_called);

  // The callback should have received stats from the first collection
  EXPECT_EQ(result_min, 10);
  EXPECT_EQ(result_max, 10);
  EXPECT_DOUBLE_EQ(result_avg, 10.0);
}

TEST(StatTrackersMinMaxAvgTests, TracksMinMaxAvgCorrectly) {
  stat_trackers::min_max_avg_tracker<int> tracker;

  int result_min = 0;
  int result_max = 0;
  double result_avg = 0;
  bool callback_called = false;

  auto callback = [&](int stat_min, int stat_max, double stat_avg) {
    result_min = stat_min;
    result_max = stat_max;
    result_avg = stat_avg;
    callback_called = true;
  };

  auto interval = std::chrono::seconds(0);

  // Collect multiple values
  tracker.collect_and_callback_on_interval(5, callback, interval);

  // Wait so interval passes
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // Collect more values (these will be the "previous" batch reported)
  tracker.collect_and_callback_on_interval(15, callback, interval);

  // First batch only had value 5
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(result_min, 5);
  EXPECT_EQ(result_max, 5);
  EXPECT_DOUBLE_EQ(result_avg, 5.0);
}

TEST(StatTrackersMinMaxAvgTests, ResetClearsState) {
  stat_trackers::min_max_avg_tracker<int> tracker;

  bool callback_called = false;
  auto callback = [&](int, int, double) {
    callback_called = true;
  };

  // Collect some values
  tracker.collect_and_callback_on_interval(100, callback, std::chrono::seconds(60));

  // Reset
  tracker.reset();

  // After reset, first collect should reinitialize timer (not trigger callback)
  tracker.collect_and_callback_on_interval(50, callback, std::chrono::seconds(0));
  EXPECT_FALSE(callback_called);
}

TEST(StatTrackersMinMaxAvgTests, MultipleValuesInBatch) {
  stat_trackers::min_max_avg_tracker<int> tracker;

  int result_min = 0;
  int result_max = 0;
  double result_avg = 0;
  bool callback_called = false;

  auto callback = [&](int stat_min, int stat_max, double stat_avg) {
    result_min = stat_min;
    result_max = stat_max;
    result_avg = stat_avg;
    callback_called = true;
  };

  // Use a longer interval so we can collect multiple values
  auto interval = std::chrono::seconds(0);

  // First call initializes timer
  tracker.collect_and_callback_on_interval(3, callback, std::chrono::seconds(60));
  tracker.collect_and_callback_on_interval(7, callback, std::chrono::seconds(60));
  tracker.collect_and_callback_on_interval(5, callback, std::chrono::seconds(60));

  EXPECT_FALSE(callback_called);

  // Now wait and trigger callback
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  tracker.collect_and_callback_on_interval(100, callback, interval);

  EXPECT_TRUE(callback_called);
  EXPECT_EQ(result_min, 3);
  EXPECT_EQ(result_max, 7);
  EXPECT_DOUBLE_EQ(result_avg, 5.0);  // (3+7+5) / 3
}

TEST(StatTrackersMinMaxAvgTests, WorksWithDoubleType) {
  stat_trackers::min_max_avg_tracker<double> tracker;

  double result_min = 0;
  double result_max = 0;
  double result_avg = 0;
  bool callback_called = false;

  auto callback = [&](double stat_min, double stat_max, double stat_avg) {
    result_min = stat_min;
    result_max = stat_max;
    result_avg = stat_avg;
    callback_called = true;
  };

  tracker.collect_and_callback_on_interval(1.5, callback, std::chrono::seconds(60));
  tracker.collect_and_callback_on_interval(2.5, callback, std::chrono::seconds(60));
  tracker.collect_and_callback_on_interval(3.5, callback, std::chrono::seconds(60));

  EXPECT_FALSE(callback_called);

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  tracker.collect_and_callback_on_interval(0.0, callback, std::chrono::seconds(0));

  EXPECT_TRUE(callback_called);
  EXPECT_DOUBLE_EQ(result_min, 1.5);
  EXPECT_DOUBLE_EQ(result_max, 3.5);
  EXPECT_DOUBLE_EQ(result_avg, 2.5);  // (1.5+2.5+3.5) / 3
}
