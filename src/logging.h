/**
 * @file src/logging.h
 * @brief Declarations for logging related functions.
 */
#pragma once

// standard includes
#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>

// lib includes
#include <boost/log/common.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/severity_logger.hpp>

/**
 * @brief Boost.Log asynchronous text sink used by Sunshine logging.
 */
using text_sink = boost::log::sinks::asynchronous_sink<boost::log::sinks::text_ostream_backend>;

namespace logging {
  /**
   * @brief Boost-managed, thread-safe verbose logging source.
   */
  BOOST_LOG_INLINE_GLOBAL_LOGGER_CTOR_ARGS(verbose_source, boost::log::sources::severity_logger_mt<int>, (0))
  /**
   * @brief Boost-managed, thread-safe debug logging source.
   */
  BOOST_LOG_INLINE_GLOBAL_LOGGER_CTOR_ARGS(debug_source, boost::log::sources::severity_logger_mt<int>, (1))
  /**
   * @brief Boost-managed, thread-safe informational logging source.
   */
  BOOST_LOG_INLINE_GLOBAL_LOGGER_CTOR_ARGS(info_source, boost::log::sources::severity_logger_mt<int>, (2))
  /**
   * @brief Boost-managed, thread-safe warning logging source.
   */
  BOOST_LOG_INLINE_GLOBAL_LOGGER_CTOR_ARGS(warning_source, boost::log::sources::severity_logger_mt<int>, (3))
  /**
   * @brief Boost-managed, thread-safe error logging source.
   */
  BOOST_LOG_INLINE_GLOBAL_LOGGER_CTOR_ARGS(error_source, boost::log::sources::severity_logger_mt<int>, (4))
  /**
   * @brief Boost-managed, thread-safe fatal logging source.
   */
  BOOST_LOG_INLINE_GLOBAL_LOGGER_CTOR_ARGS(fatal_source, boost::log::sources::severity_logger_mt<int>, (5))
#ifdef SUNSHINE_TESTS
  /**
   * @brief Boost-managed source for test output.
   */
  BOOST_LOG_INLINE_GLOBAL_LOGGER_CTOR_ARGS(tests_source, boost::log::sources::severity_logger_mt<int>, (10))
#endif

  /**
   * @brief Immutable severity handle preserving existing BOOST_LOG call sites.
   *
   * Boost.Log global storage initializes and synchronizes the mutable source.
   */
  class logger_t {
  public:
    using logger_type = boost::log::sources::severity_logger_mt<int>;  ///< Managed source type.
    using char_type = logger_type::char_type;  ///< Character type required by Boost.Log.

    /**
     * @brief Select the managed source for a severity.
     * @param severity Default severity for this handle.
     */
    constexpr explicit logger_t(int severity):
        severity_ {severity} {
    }

    /**
     * @brief Access the Boost-managed source for this severity.
     * @return Thread-safe logging source.
     */
    logger_type &get() const {
      switch (severity_) {
        case 0:
          return verbose_source::get();
        case 1:
          return debug_source::get();
        case 2:
          return info_source::get();
        case 3:
          return warning_source::get();
        case 4:
          return error_source::get();
        case 5:
          return fatal_source::get();
#ifdef SUNSHINE_TESTS
        case 10:
          return tests_source::get();
#endif
        default:
          throw std::invalid_argument {"Unsupported log severity"};
      }
    }

    /**
     * @brief Convert a severity handle for periodic logger construction.
     * @return Managed logging source.
     */
    operator logger_type &() const {
      return get();
    }

    /**
     * @brief Open a record using the managed source.
     * @return Record accepted by the configured sinks.
     */
    boost::log::record open_record() const {
      return get().open_record();
    }

    /**
     * @brief Open a record with Boost.Log named arguments.
     * @param args Named attributes supplied by Boost.Log.
     * @return Record accepted by the configured sinks.
     */
    template<typename Args>
    boost::log::record open_record(const Args &args) const {
      return get().open_record(args);
    }

    /**
     * @brief Submit a completed record to the configured sinks.
     * @param record Record to publish.
     */
    void push_record(boost::log::record &&record) const {
      get().push_record(std::move(record));
    }

  private:
    int severity_;  ///< Severity selected by the immutable handle.
  };
}  // namespace logging

inline constexpr logging::logger_t verbose {0};  ///< Verbose.
inline constexpr logging::logger_t debug {1};  ///< Debug.
inline constexpr logging::logger_t info {2};  ///< Info.
inline constexpr logging::logger_t warning {3};  ///< Warning.
inline constexpr logging::logger_t error {4};  ///< Error.
inline constexpr logging::logger_t fatal {5};  ///< Fatal.
#ifdef SUNSHINE_TESTS
inline constexpr logging::logger_t tests {10};  ///< Automatic test output.
#endif

#include "config.h"
#include "stat_trackers.h"

/**
 * @brief Handles the initialization and deinitialization of the logging system.
 */
namespace logging {
  /**
   * @brief The number of previous log files retained during rotation.
   */
  inline constexpr std::size_t retained_log_file_count {5};

  /**
   * @brief Rotate a log file while retaining up to five previous logs.
   *
   * The current log is renamed with a `.1` suffix, existing rotated logs are
   * advanced by one generation, and the previous `.5` log is removed.
   *
   * @param log_file Path to the current log file.
   * @return An error code when rotation fails, or a clear error code on success.
   */
  inline std::error_code rotate_log_file(const std::filesystem::path &log_file) noexcept {
    const auto rotated_log_path = [&log_file](std::size_t generation) {
      auto rotated_path = log_file;
      rotated_path += std::format(".{}", generation);
      return rotated_path;
    };

    try {
      std::filesystem::remove(rotated_log_path(retained_log_file_count));

      for (auto generation = retained_log_file_count; generation > 1; --generation) {
        const auto previous_path = rotated_log_path(generation - 1);
        if (std::filesystem::exists(previous_path)) {
          std::filesystem::rename(previous_path, rotated_log_path(generation));
        }
      }

      if (std::filesystem::exists(log_file)) {
        std::filesystem::rename(log_file, rotated_log_path(1));
      }

      return {};
    } catch (const std::filesystem::filesystem_error &filesystem_error) {
      return filesystem_error.code();
    }
  }

  /**
   * @brief RAII helper that runs shutdown cleanup when destroyed.
   */
  class deinit_t {
  public:
    /**
     * @brief Own the sinks registered by one logging initialization.
     * @param sink Asynchronous text sink.
     * @param platform_sink Additional platform sink, when present.
     */
    deinit_t(boost::shared_ptr<text_sink> sink, boost::shared_ptr<boost::log::sinks::sink> platform_sink = {});

    deinit_t(const deinit_t &) = delete;
    deinit_t &operator=(const deinit_t &) = delete;
    /**
     * @brief Restores logging state when the logging subsystem shuts down.
     */
    ~deinit_t();

  private:
    boost::shared_ptr<text_sink> sink_;  ///< Sink whose registration this owner controls.
    boost::shared_ptr<boost::log::sinks::sink> platform_sink_;  ///< Optional platform sink.
  };

  /**
   * @brief Format a Boost.Log record for Sunshine log output.
   *
   * @param view Boost.Log record view being formatted.
   * @param os Boost.Log output stream receiving formatted text.
   */
  void formatter(const boost::log::record_view &view, boost::log::formatting_ostream &os);

  /**
   * @brief Rotate the current log file and initialize the logging system.
   * @param min_log_level The minimum log level to output.
   * @param log_file The log file to write to.
   * @return An object that will deinitialize the logging system when it goes out of scope.
   * @examples
   * log_init(2, "sunshine.log");
   * @examples_end
   */
  [[nodiscard]] std::unique_ptr<deinit_t> init(int min_log_level, const std::string &log_file);

  /**
   * @brief Setup AV logging.
   * @param min_log_level The log level.
   */
  void setup_av_logging(int min_log_level);

  /**
   * @brief Setup logging for libdisplaydevice.
   * @param min_log_level The log level.
   */
  void setup_libdisplaydevice_logging(int min_log_level);

  /**
   * @brief Flush the log.
   * @examples
   * log_flush();
   * @examples_end
   */
  void log_flush();

  /**
   * @brief Print help to stdout.
   * @param name The name of the program.
   * @examples
   * print_help("sunshine");
   * @examples_end
   */
  void print_help(const char *name);

  /**
   * @brief A helper class for tracking and logging numerical values across a period of time
   * @examples
   * min_max_avg_periodic_logger<int> logger(debug, "Test time value", "ms", 5s);
   * logger.collect_and_log(1);
   * // ...
   * logger.collect_and_log(2);
   * // after 5 seconds
   * logger.collect_and_log(3);
   * // In the log:
   * // [2024:01:01:12:00:00]: Debug: Test time value (min/max/avg): 1ms/3ms/2.00ms
   * @examples_end
   */
  template<typename T>
  class min_max_avg_periodic_logger {
  public:
    /**
     * @brief Construct a periodic logger that reports min, max, and average samples.
     *
     * @param severity Severity level associated with the log message.
     * @param message Message text to log or report.
     * @param units Unit label appended to tracked numeric values.
     * @param interval_in_seconds Interval in seconds.
     */
    min_max_avg_periodic_logger(boost::log::sources::severity_logger_mt<int> &severity, std::string_view message, std::string_view units, std::chrono::seconds interval_in_seconds = std::chrono::seconds(20)):
        severity(severity),
        message(message),
        units(units),
        interval(interval_in_seconds),
        enabled(config::sunshine.min_log_level <= severity.default_severity()) {
    }

    /**
     * @brief Collect a metric sample and write it to the periodic log when due.
     *
     * @param value Numeric sample to include in the next periodic aggregate.
     */
    void collect_and_log(const T &value) {
      if (enabled) {
        auto print_info = [&](const T &min_value, const T &max_value, double avg_value) {
          auto f = stat_trackers::two_digits_after_decimal();
          if constexpr (std::is_floating_point_v<T>) {
            BOOST_LOG(severity.get()) << message << " (min/max/avg): " << f % min_value << units << "/" << f % max_value << units << "/" << f % avg_value << units;
          } else {
            BOOST_LOG(severity.get()) << message << " (min/max/avg): " << min_value << units << "/" << max_value << units << "/" << f % avg_value << units;
          }
        };
        tracker.collect_and_callback_on_interval(value, print_info, interval);
      }
    }

    /**
     * @brief Collect a metric sample and write it to the periodic log when due.
     *
     * @param func Callable used to calculate the tracked sample value.
     */
    void collect_and_log(std::function<T()> func) {
      if (enabled) {
        collect_and_log(func());
      }
    }

    /**
     * @brief Reset the object to its initial empty state.
     */
    void reset() {
      if (enabled) {
        tracker.reset();
      }
    }

    /**
     * @brief Check whether this periodic logger should emit at the configured severity.
     *
     * @return True when the logger severity is enabled and sampling should continue.
     */
    bool is_enabled() const {
      return enabled;
    }

  private:
    std::reference_wrapper<boost::log::sources::severity_logger_mt<int>> severity;
    std::string message;
    std::string units;
    std::chrono::seconds interval;
    bool enabled;
    stat_trackers::min_max_avg_tracker<T> tracker;
  };

  /**
   * @brief A helper class for tracking and logging short time intervals across a period of time
   * @examples
   * time_delta_periodic_logger logger(debug, "Test duration", 5s);
   * logger.first_point_now();
   * // ...
   * logger.second_point_now_and_log();
   * // after 5 seconds
   * logger.first_point_now();
   * // ...
   * logger.second_point_now_and_log();
   * // In the log:
   * // [2024:01:01:12:00:00]: Debug: Test duration (min/max/avg): 1.23ms/3.21ms/2.31ms
   * @examples_end
   */
  class time_delta_periodic_logger {
  public:
    /**
     * @brief Construct a periodic logger for elapsed-time samples.
     *
     * @param severity Severity level associated with the log message.
     * @param message Message text to log or report.
     * @param interval_in_seconds Interval in seconds.
     */
    time_delta_periodic_logger(boost::log::sources::severity_logger_mt<int> &severity, std::string_view message, std::chrono::seconds interval_in_seconds = std::chrono::seconds(20)):
        logger(severity, message, "ms", interval_in_seconds) {
    }

    /**
     * @brief Store the first timestamp for a measured interval.
     *
     * @param point Time point used for elapsed-time calculations.
     */
    void first_point(const std::chrono::steady_clock::time_point &point) {
      if (logger.is_enabled()) {
        point1 = point;
      }
    }

    /**
     * @brief Store the current time as the first timestamp.
     */
    void first_point_now() {
      if (logger.is_enabled()) {
        first_point(std::chrono::steady_clock::now());
      }
    }

    /**
     * @brief Store the second timestamp and log the elapsed interval.
     *
     * @param point Time point used for elapsed-time calculations.
     */
    void second_point_and_log(const std::chrono::steady_clock::time_point &point) {
      if (logger.is_enabled()) {
        logger.collect_and_log(std::chrono::duration<double, std::milli>(point - point1).count());
      }
    }

    /**
     * @brief Store the current time as the second timestamp and log the elapsed interval.
     */
    void second_point_now_and_log() {
      if (logger.is_enabled()) {
        second_point_and_log(std::chrono::steady_clock::now());
      }
    }

    /**
     * @brief Reset the object to its initial empty state.
     */
    void reset() {
      if (logger.is_enabled()) {
        logger.reset();
      }
    }

    /**
     * @brief Check whether this periodic logger should emit at the configured severity.
     *
     * @return True when the logger severity is enabled and sampling should continue.
     */
    bool is_enabled() const {
      return logger.is_enabled();
    }

  private:
    std::chrono::steady_clock::time_point point1 = std::chrono::steady_clock::now();
    min_max_avg_periodic_logger<double> logger;
  };

  /**
   * @brief Enclose string in square brackets.
   * @param input Input string.
   * @return Enclosed string.
   */
  std::string bracket(const std::string &input);

  /**
   * @brief Enclose string in square brackets.
   * @param input Input string.
   * @return Enclosed string.
   */
  std::wstring bracket(const std::wstring &input);

}  // namespace logging
