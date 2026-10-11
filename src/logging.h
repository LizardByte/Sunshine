/**
 * @file src/logging.h
 * @brief Declarations for logging related functions.
 */
#pragma once

// standard includes
#include <chrono>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

/**
 * @brief Application logging functions and independently registered outputs.
 *
 * Text and ordinary values can be logged directly. Formatting runs only when
 * an output accepts the severity; use a builder to defer expensive arguments.
 * @code
 * logging::info("Server started");
 * logging::warning("Unknown preset: {}", preset);
 * logging::debug([](std::ostream &output) {
 *   output << collect_diagnostics();
 * });
 * @endcode
 */
namespace logging {
  /**
   * @brief Severity levels shared by every logging output.
   */
  enum class level_e {
    verbose = 0,  ///< Verbose diagnostics.
    debug = 1,  ///< Debug diagnostics.
    info = 2,  ///< Informational messages.
    warning = 3,  ///< Recoverable problems.
    error = 4,  ///< Errors.
    fatal = 5,  ///< Fatal errors.
  };

  /**
   * @brief A structured message delivered to a registered logging sink.
   */
  struct message_t {
    level_e level;  ///< Severity of the message.
    std::chrono::system_clock::time_point timestamp;  ///< Time the record was opened.
    std::string text;  ///< UTF-8 message text, without the timestamp or severity prefix.
  };

  /**
   * @brief Interface for additional outputs such as a webhook or a test buffer.
   */
  class sink_t {
  public:
    /**
     * @brief Destroy a logging output.
     */
    virtual ~sink_t() = default;

    /**
     * @brief Consume an accepted message on this sink's delivery thread.
     * @param message Structured logging message, valid for this call; copy it to retain it.
     */
    virtual void consume(const message_t &message) = 0;
  };

  /**
   * @brief Format a structured message using Sunshine's timestamp and severity prefix.
   * @param message Structured logging message.
   * @return A formatted line without an added trailing newline.
   */
  std::string format_message(const message_t &message);

  /**
   * @brief Check whether any registered output accepts a severity.
   * @param level Severity to check.
   * @return True when a record at this level is accepted.
   * @throws std::invalid_argument If this build does not support the severity.
   */
  bool is_enabled(level_e level);

  namespace detail {
    /**
     * @brief Type-erased, synchronous message writer.
     */
    using writer_t = void (*)(const void *, std::ostream &);

    /**
     * @brief Open a record and run its writer only when an output accepts it.
     * @param level Message severity.
     * @param context Writer object borrowed for the duration of the call.
     * @param writer Function that writes the message.
     */
    void write(level_e level, const void *context, writer_t writer);
  }  // namespace detail

  /**
   * @brief Log a message without evaluating its builder when the severity is disabled.
   * @tparam Builder A callable object accepting a standard output stream.
   * @param level Message severity.
   * @param builder Message builder invoked synchronously at most once.
   * @throws std::invalid_argument If this build does not support the severity.
   */
  template<typename Builder>
    requires std::is_object_v<Builder> && std::invocable<const Builder &, std::ostream &>
  void log(level_e level, const Builder &builder) {
    detail::write(level, &builder, [](const void *context, std::ostream &output) {
      (*static_cast<const Builder *>(context))(output);
    });
  }

  /**
   * @brief Log an already available message.
   * @param level Message severity.
   * @param message UTF-8 message text.
   */
  inline void log(level_e level, std::string_view message) {
    log(level, [message](std::ostream &output) {
      output << message;
    });
  }

  /**
   * @brief Format and log a message when an output accepts its severity.
   * @tparam Args Types of the formatting arguments.
   * @param level Message severity.
   * @param format Compile-time checked format string.
   * @param args Formatting arguments; use a builder to defer computing these values.
   */
  template<typename... Args>
    requires(sizeof...(Args) > 0)
  void log(level_e level, std::format_string<Args...> format, Args &&...args) {
    log(level, [&format, &args...](std::ostream &output) {
      output << std::format(format, std::forward<Args>(args)...);
    });
  }

  /**
   * @brief Write a verbose message through the configured outputs.
   * @tparam Message Type of the text or lazy stream builder.
   * @param message Text or a lazy stream builder.
   */
  template<typename Message>
  void verbose(Message &&message) {
    log(level_e::verbose, std::forward<Message>(message));
  }

  /**
   * @brief Format a verbose message when its severity is enabled.
   * @tparam Args Types of the formatting arguments.
   * @param format Compile-time checked format string.
   * @param args Formatting arguments; use a builder to defer computing these values.
   */
  template<typename... Args>
    requires(sizeof...(Args) > 0)
  void verbose(std::format_string<Args...> format, Args &&...args) {
    log(level_e::verbose, format, std::forward<Args>(args)...);
  }

  /**
   * @brief Write a debug message through the configured outputs.
   * @tparam Message Type of the text or lazy stream builder.
   * @param message Text or a lazy stream builder.
   */
  template<typename Message>
  void debug(Message &&message) {
    log(level_e::debug, std::forward<Message>(message));
  }

  /**
   * @brief Format a debug message when its severity is enabled.
   * @tparam Args Types of the formatting arguments.
   * @param format Compile-time checked format string.
   * @param args Formatting arguments; use a builder to defer computing these values.
   */
  template<typename... Args>
    requires(sizeof...(Args) > 0)
  void debug(std::format_string<Args...> format, Args &&...args) {
    log(level_e::debug, format, std::forward<Args>(args)...);
  }

  /**
   * @brief Write an informational message through the configured outputs.
   * @tparam Message Type of the text or lazy stream builder.
   * @param message Text or a lazy stream builder.
   */
  template<typename Message>
  void info(Message &&message) {
    log(level_e::info, std::forward<Message>(message));
  }

  /**
   * @brief Format an informational message when its severity is enabled.
   * @tparam Args Types of the formatting arguments.
   * @param format Compile-time checked format string.
   * @param args Formatting arguments; use a builder to defer computing these values.
   */
  template<typename... Args>
    requires(sizeof...(Args) > 0)
  void info(std::format_string<Args...> format, Args &&...args) {
    log(level_e::info, format, std::forward<Args>(args)...);
  }

  /**
   * @brief Write a warning message through the configured outputs.
   * @tparam Message Type of the text or lazy stream builder.
   * @param message Text or a lazy stream builder.
   */
  template<typename Message>
  void warning(Message &&message) {
    log(level_e::warning, std::forward<Message>(message));
  }

  /**
   * @brief Format a warning message when its severity is enabled.
   * @tparam Args Types of the formatting arguments.
   * @param format Compile-time checked format string.
   * @param args Formatting arguments; use a builder to defer computing these values.
   */
  template<typename... Args>
    requires(sizeof...(Args) > 0)
  void warning(std::format_string<Args...> format, Args &&...args) {
    log(level_e::warning, format, std::forward<Args>(args)...);
  }

  /**
   * @brief Write an error message through the configured outputs.
   * @tparam Message Type of the text or lazy stream builder.
   * @param message Text or a lazy stream builder.
   */
  template<typename Message>
  void error(Message &&message) {
    log(level_e::error, std::forward<Message>(message));
  }

  /**
   * @brief Format an error message when its severity is enabled.
   * @tparam Args Types of the formatting arguments.
   * @param format Compile-time checked format string.
   * @param args Formatting arguments; use a builder to defer computing these values.
   */
  template<typename... Args>
    requires(sizeof...(Args) > 0)
  void error(std::format_string<Args...> format, Args &&...args) {
    log(level_e::error, format, std::forward<Args>(args)...);
  }

  /**
   * @brief Write a fatal message through the configured outputs.
   * @tparam Message Type of the text or lazy stream builder.
   * @param message Text or a lazy stream builder.
   */
  template<typename Message>
  void fatal(Message &&message) {
    log(level_e::fatal, std::forward<Message>(message));
  }

  /**
   * @brief Format a fatal message when its severity is enabled.
   * @tparam Args Types of the formatting arguments.
   * @param format Compile-time checked format string.
   * @param args Formatting arguments; use a builder to defer computing these values.
   */
  template<typename... Args>
    requires(sizeof...(Args) > 0)
  void fatal(std::format_string<Args...> format, Args &&...args) {
    log(level_e::fatal, format, std::forward<Args>(args)...);
  }

#ifdef SUNSHINE_TESTS
  inline constexpr level_e test_level {static_cast<level_e>(10)};  ///< Severity reserved for test output.

  /**
   * @brief Write a test event through the configured outputs.
   * @tparam Message Type of the text or lazy stream builder.
   * @param message Text or a lazy stream builder.
   */
  template<typename Message>
  void tests(Message &&message) {
    log(test_level, std::forward<Message>(message));
  }

  /**
   * @brief Format a test message when its severity is enabled.
   * @tparam Args Types of the formatting arguments.
   * @param format Compile-time checked format string.
   * @param args Formatting arguments; use a builder to defer computing these values.
   */
  template<typename... Args>
    requires(sizeof...(Args) > 0)
  void tests(std::format_string<Args...> format, Args &&...args) {
    log(test_level, format, std::forward<Args>(args)...);
  }

  namespace testing {
    /**
     * @brief Temporarily disable logging while testing lazy message construction.
     */
    class scoped_disable_t {
    public:
      /**
       * @brief Disable logging and remember its previous enabled state.
       */
      scoped_disable_t();
      scoped_disable_t(const scoped_disable_t &) = delete;
      scoped_disable_t &operator=(const scoped_disable_t &) = delete;
      /**
       * @brief Restore the logging state that preceded this scope.
       */
      ~scoped_disable_t();

    private:
      bool previous_;  ///< Enabled state restored when the scope ends.
    };
  }  // namespace testing
#endif
}  // namespace logging

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
    deinit_t(const deinit_t &) = delete;
    deinit_t &operator=(const deinit_t &) = delete;
    /**
     * @brief Unregister and flush only the outputs owned by this registration.
     */
    ~deinit_t();

  private:
    /**
     * @brief Backend-specific output ownership.
     */
    struct impl_t;
    /**
     * @brief Take ownership of the registered backend outputs.
     * @param impl Backend-specific output ownership.
     */
    explicit deinit_t(std::unique_ptr<impl_t> impl);
    std::unique_ptr<impl_t> impl_;  ///< Backend-specific output ownership.

    friend std::unique_ptr<deinit_t> init(int min_log_level, const std::string &log_file);
    friend std::unique_ptr<deinit_t> add_sink(std::shared_ptr<sink_t> sink, int min_log_level, bool asynchronous);
  };

  /**
   * @brief Register an additional output without changing existing logging callers.
   *
   * Keep the returned owner alive while the output is needed. Destroying it
   * unregisters the output and drains any queued messages.
   *
   * @param sink Output receiving structured messages.
   * @param min_log_level Minimum accepted severity.
   * @param asynchronous Queue messages for a worker thread when true; flushing may deliver on the caller.
   * @return Owner of this output registration.
   * @throws std::invalid_argument If the output is null.
   */
  [[nodiscard]] std::unique_ptr<deinit_t> add_sink(std::shared_ptr<sink_t> sink, int min_log_level = 0, bool asynchronous = true);

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
   * min_max_avg_periodic_logger<int> logger(level_e::debug, "Test time value", "ms", 5s);
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
    min_max_avg_periodic_logger(level_e severity, std::string_view message, std::string_view units, std::chrono::seconds interval_in_seconds = std::chrono::seconds(20)):
        severity(severity),
        message(message),
        units(units),
        interval(interval_in_seconds),
        enabled(logging::is_enabled(severity)) {
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
            log(severity, [this, &f, &min_value, &max_value, &avg_value](std::ostream &output) {
              output << message << " (min/max/avg): " << f % min_value << units << "/" << f % max_value << units << "/" << f % avg_value << units;
            });
          } else {
            log(severity, [this, &f, &min_value, &max_value, &avg_value](std::ostream &output) {
              output << message << " (min/max/avg): " << min_value << units << "/" << max_value << units << "/" << f % avg_value << units;
            });
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
    level_e severity;  ///< Severity used for periodic messages.
    std::string message;
    std::string units;
    std::chrono::seconds interval;
    bool enabled;
    stat_trackers::min_max_avg_tracker<T> tracker;
  };

  /**
   * @brief A helper class for tracking and logging short time intervals across a period of time
   * @examples
   * time_delta_periodic_logger logger(level_e::debug, "Test duration", 5s);
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
    time_delta_periodic_logger(level_e severity, std::string_view message, std::chrono::seconds interval_in_seconds = std::chrono::seconds(20)):
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
