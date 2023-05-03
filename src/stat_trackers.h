#pragma once

#include <chrono>
#include <functional>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

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
      std::chrono::steady_clock::time_point last_callback_time = std::chrono::steady_clock::now();
      T stat_min = std::numeric_limits<T>::max();
      T stat_max = 0;
      double stat_total = 0;
      uint32_t calls = 0;
    } data;
  };

  template <typename... Types>
  class count_each_value_tracker {
  public:
    using callback_function = std::function<void(const std::map<Types, uint32_t> &...each_value_count)>;

    void
    collect_and_callback_on_interval(const Types &...values, const callback_function &callback, std::chrono::seconds interval_in_seconds) {
      collect_and_callback_internal(std::tie(values...), std::make_index_sequence<sizeof...(Types)>(), callback, interval_in_seconds);
    }

  private:
    template <std::size_t... Indices>
    void
    collect_and_callback_internal(std::tuple<const Types &...> values, std::index_sequence<Indices...>, const callback_function &callback, std::chrono::seconds interval_in_seconds) {
      if (std::chrono::steady_clock::now() > data.last_callback_time + interval_in_seconds) {
        std::apply(callback, data.each_value_count);
        data = {};
      }
      // Use fold expression to iterate through variadic template
      ((std::get<Indices>(data.each_value_count)[std::get<Indices>(values)] += 1), ...);
    }

    struct {
      std::chrono::steady_clock::time_point last_callback_time = std::chrono::steady_clock::now();
      std::tuple<std::map<Types, uint32_t>...> each_value_count;
    } data;
  };

}  // namespace stat_trackers
