/**
 * @file src/stat_trackers.h
 * @brief todo
 */
#pragma once

#include <chrono>
#include <functional>
#include <limits>

#include <boost/format.hpp>

namespace stat_trackers {

  boost::format
  one_digit_after_decimal();

  template <typename T>
  class min_max_avg_tracker {
  public:
    using callback_function = std::function<void(T stat_min, T stat_max, double stat_avg)>;

    void
    collect_and_callback_on_interval(T stat, const callback_function &callback, std::chrono::seconds interval_in_seconds) {
      if (std::chrono::steady_clock::now() > data.last_callback_time + interval_in_seconds) {
        callback(data.stat_min, data.stat_max, data.stat_total / data.calls);
        data = {};
      }
      data.stat_min = std::min(data.stat_min, stat);
      data.stat_max = std::max(data.stat_max, stat);
      data.stat_total += stat;
      data.calls += 1;
    }

  private:
    struct {
      std::chrono::steady_clock::steady_clock::time_point last_callback_time = std::chrono::steady_clock::now();
      T stat_min = std::numeric_limits<T>::max();
      T stat_max = 0;
      double stat_total = 0;
      uint32_t calls = 0;
    } data;
  };

}  // namespace stat_trackers
