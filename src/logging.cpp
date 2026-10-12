/**
 * @file src/logging.cpp
 * @brief Definitions for logging related functions.
 * @note Logging outputs receive the severity enum's underlying integer value.
 */
// standard includes
#include <array>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

// lib includes
#include <boost/core/null_deleter.hpp>
#include <boost/format.hpp>
#include <boost/log/attributes/function.hpp>
#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/utility/exception_handler.hpp>

// local includes
#include "logging.h"

// conditional includes
#ifdef __ANDROID__
  #include <android/log.h>
#else
  #include <display_device/logging.h>
#endif

extern "C" {
#include <libavutil/log.h>
}

using namespace std::literals;

namespace bl = boost::log;

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", int)
BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "Timestamp", std::chrono::system_clock::time_point)

namespace logging {
  namespace {
    /**
     * @brief Boost-managed source shared by all Sunshine logging functions.
     */
    BOOST_LOG_INLINE_GLOBAL_LOGGER_INIT(log_source, bl::sources::severity_logger_mt<int>) {
      logger_type source;
      source.add_attribute("Timestamp", bl::attributes::make_function([]() {
                             return std::chrono::system_clock::now();
                           }));
      return source;
    }

    /**
     * @brief Validate a severity and select its displayed name.
     * @param level Message severity.
     * @return Name prefixed to formatted messages.
     * @throws std::invalid_argument If this build does not support the severity.
     */
    std::string_view level_name(level_e level) {
#ifdef SUNSHINE_TESTS
      if (level == test_level) {
        return "Tests"sv;
      }
#endif
      switch (level) {
        case level_e::verbose:
          return "Verbose"sv;
        case level_e::debug:
          return "Debug"sv;
        case level_e::info:
          return "Info"sv;
        case level_e::warning:
          return "Warning"sv;
        case level_e::error:
          return "Error"sv;
        case level_e::fatal:
          return "Fatal"sv;
        default:
          throw std::invalid_argument {"Unsupported log severity"};
      }
    }

    /**
     * @brief Convert a backend record into the public structured message.
     * @param record Accepted backend record.
     * @return Message shared with every registered output.
     */
    message_t make_message(const bl::record_view &record) {
      return {
        static_cast<level_e>(record[severity].get()),
        record[timestamp].get(),
        record[bl::expressions::smessage].get(),
      };
    }

    /**
     * @brief Format a backend record for the existing file and console outputs.
     * @param record Backend record being formatted.
     * @param output Stream receiving the formatted line.
     */
    void formatter(const bl::record_view &record, bl::formatting_ostream &output) {
      output << format_message(make_message(record));
    }

    /**
     * @brief Adapt an application output to the Boost delivery machinery.
     */
    class output_backend_t: public bl::sinks::basic_sink_backend<bl::sinks::synchronized_feeding> {
    public:
      /**
       * @brief Retain the application output while it is registered.
       * @param output Application output receiving messages.
       */
      explicit output_backend_t(std::shared_ptr<sink_t> output):
          output_ {std::move(output)} {}

      /**
       * @brief Deliver one structured message to the application output.
       * @param record Accepted backend record.
       */
      void consume(const bl::record_view &record) {
        output_->consume(make_message(record));
      }

    private:
      std::shared_ptr<sink_t> output_;  ///< Application output retained by this backend.
    };

    /**
     * @brief Asynchronous file and console output.
     */
    using text_sink = bl::sinks::asynchronous_sink<bl::sinks::text_ostream_backend>;
  }  // namespace

  /**
   * @brief Own backend registrations and their optional worker shutdown functions.
   */
  struct deinit_t::impl_t {
    /**
     * @brief One registered output and its worker shutdown operation.
     */
    struct registration_t {
      boost::shared_ptr<bl::sinks::sink> sink;  ///< Registered output.
      std::function<void()> stop;  ///< Stops a worker thread, when present.
    };

    std::vector<registration_t> registrations;  ///< Outputs owned by this registration.
  };

  deinit_t::deinit_t(std::unique_ptr<impl_t> impl):
      impl_ {std::move(impl)} {}

  deinit_t::~deinit_t() {
    auto core = bl::core::get();
    for (auto &registration : impl_->registrations) {
      core->remove_sink(registration.sink);
    }
    for (auto &registration : impl_->registrations) {
      if (registration.stop) {
        registration.stop();
      }
      registration.sink->flush();
    }
  }

  std::string format_message(const message_t &message) {
    const auto seconds = std::chrono::floor<std::chrono::seconds>(message.timestamp);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(message.timestamp - seconds).count();
    const auto time = std::chrono::system_clock::to_time_t(seconds);
    std::tm local_time {};
#ifdef _WIN32
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    std::ostringstream output;
    output << '[' << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S.")
           << std::setfill('0') << std::setw(3) << milliseconds
           << "]: " << level_name(message.level) << ": " << message.text;
    return output.str();
  }

  bool is_enabled(level_e level) {
    static_cast<void>(level_name(level));
    return static_cast<bool>(log_source::get().open_record(bl::keywords::severity = std::to_underlying(level)));
  }

  void detail::write(level_e level, const writer_t &writer) {
    static_cast<void>(level_name(level));
    auto &source = log_source::get();
    auto record = source.open_record(bl::keywords::severity = std::to_underlying(level));
    if (!record) {
      return;
    }
    std::ostringstream message;
    writer.write(message);
    bl::record_ostream output {record};
    output << message.view();
    output.flush();
    source.push_record(std::move(record));
  }

  std::unique_ptr<deinit_t> add_sink(std::shared_ptr<sink_t> sink, int min_log_level, bool asynchronous) {
    if (!sink) {
      throw std::invalid_argument {"A logging output must not be null"};
    }
    auto backend = boost::make_shared<output_backend_t>(std::move(sink));
    auto impl = std::make_unique<deinit_t::impl_t>();
    if (asynchronous) {
      auto output = boost::make_shared<bl::sinks::asynchronous_sink<output_backend_t>>(backend);
      output->set_filter(severity >= min_log_level);
      output->set_exception_handler(bl::make_exception_suppressor());
      impl->registrations.push_back({output, [output]() {
                                       output->stop();
                                     }});
    } else {
      auto output = boost::make_shared<bl::sinks::synchronous_sink<output_backend_t>>(backend);
      output->set_filter(severity >= min_log_level);
      output->set_exception_handler(bl::make_exception_suppressor());
      impl->registrations.push_back({output, {}});
    }
    auto owner = std::unique_ptr<deinit_t> {new deinit_t {std::move(impl)}};
    bl::core::get()->add_sink(owner->impl_->registrations.front().sink);
    return owner;
  }

#ifdef SUNSHINE_TESTS
  testing::scoped_disable_t::scoped_disable_t():
      previous_ {bl::core::get()->get_logging_enabled()} {
    bl::core::get()->set_logging_enabled(false);
  }

  testing::scoped_disable_t::~scoped_disable_t() {
    bl::core::get()->set_logging_enabled(previous_);
  }
#endif
#ifdef __ANDROID__
  namespace sinks = boost::log::sinks;
  namespace expr = boost::log::expressions;

  void android_log(const std::string &message, int severity) {
    android_LogPriority android_priority;
    switch (severity) {
      case 0:
        android_priority = ANDROID_LOG_VERBOSE;
        break;
      case 1:
        android_priority = ANDROID_LOG_DEBUG;
        break;
      case 2:
        android_priority = ANDROID_LOG_INFO;
        break;
      case 3:
        android_priority = ANDROID_LOG_WARN;
        break;
      case 4:
        android_priority = ANDROID_LOG_ERROR;
        break;
      case 5:
        android_priority = ANDROID_LOG_FATAL;
        break;
      default:
        android_priority = ANDROID_LOG_UNKNOWN;
        break;
    }
    __android_log_print(android_priority, "Sunshine", "%s", message.c_str());
  }

  // custom sink backend for android
  struct android_sink_backend: public sinks::basic_sink_backend<sinks::concurrent_feeding> {
    void consume(const bl::record_view &rec) {
      int log_sev = rec[severity].get();
      const std::string log_msg = rec[expr::smessage].get();
      // log to android
      android_log(log_msg, log_sev);
    }
  };
#endif

  [[nodiscard]] std::unique_ptr<deinit_t> init(int min_log_level, const std::string &log_file) {
    const auto log_path = std::filesystem::path {std::u8string {log_file.begin(), log_file.end()}};
    if (const auto rotation_error = rotate_log_file(log_path)) {
      std::cerr << "Failed to rotate log file '" << log_file << "': " << rotation_error.message() << '\n';
    }

#ifndef __ANDROID__
    setup_av_logging(min_log_level);
    setup_libdisplaydevice_logging(min_log_level);
#endif

    auto sink = boost::make_shared<text_sink>();

#ifndef SUNSHINE_TESTS
    boost::shared_ptr<std::ostream> stream {&std::cout, boost::null_deleter()};
    sink->locked_backend()->add_stream(stream);
#endif

    sink->locked_backend()->add_stream(boost::make_shared<std::ofstream>(log_file));
    sink->set_filter(severity >= min_log_level);
    sink->set_formatter(&formatter);

    // Prevent the async sink's background thread from dying on backend exceptions.
    // Without this, a single I/O error (disk full, file locked, broken stdout, etc.)
    // kills the thread and all subsequent log records are silently lost.
    sink->set_exception_handler(bl::make_exception_suppressor());

    // Flush after each log record to ensure log file contents on disk isn't stale.
    // This is particularly important when running from a Windows service.
    sink->locked_backend()->auto_flush(true);

#ifdef __ANDROID__
    auto platform_sink = boost::make_shared<sinks::synchronous_sink<android_sink_backend>>();
#endif
    auto impl = std::make_unique<deinit_t::impl_t>();
    impl->registrations.push_back({sink, [sink]() {
                                     sink->stop();
                                   }});
#ifdef __ANDROID__
    impl->registrations.push_back({platform_sink, {}});
#endif
    auto owner = std::unique_ptr<deinit_t> {new deinit_t {std::move(impl)}};
    bl::core::get()->add_sink(sink);
#ifdef __ANDROID__
    bl::core::get()->add_sink(platform_sink);
#endif
    return owner;
  }

#ifndef __ANDROID__
  void setup_av_logging(int min_log_level) {
    if (min_log_level >= 1) {
      av_log_set_level(AV_LOG_QUIET);
    } else {
      av_log_set_level(AV_LOG_DEBUG);
    }
    av_log_set_callback([](void *ptr, int level, const char *fmt, va_list vl) {
      static int print_prefix = 1;
      std::array<char, 1024> buffer;

      av_log_format_line(ptr, level, fmt, vl, buffer.data(), buffer.size(), &print_prefix);
      if (level <= AV_LOG_ERROR) {
        // We print AV_LOG_FATAL at the error level. FFmpeg prints things as fatal that
        // are expected in some cases, such as lack of codec support or similar things.
        logging::error(buffer.data());
      } else if (level <= AV_LOG_WARNING) {
        logging::warning(buffer.data());
      } else if (level <= AV_LOG_INFO) {
        logging::info(buffer.data());
      } else if (level <= AV_LOG_VERBOSE) {
        // AV_LOG_VERBOSE is less verbose than AV_LOG_DEBUG
        logging::debug(buffer.data());
      } else {
        logging::verbose(buffer.data());
      }
    });
  }

  void setup_libdisplaydevice_logging(int min_log_level) {
    constexpr int min_level {static_cast<int>(display_device::Logger::LogLevel::verbose)};
    constexpr int max_level {static_cast<int>(display_device::Logger::LogLevel::fatal)};
    const auto log_level {static_cast<display_device::Logger::LogLevel>(std::min(std::max(min_level, min_log_level), max_level))};

    display_device::Logger::get().setLogLevel(log_level);
    display_device::Logger::get().setCustomCallback([](const display_device::Logger::LogLevel level, const std::string &message) {
      switch (level) {
        case display_device::Logger::LogLevel::verbose:
          logging::verbose(message);
          break;
        case display_device::Logger::LogLevel::debug:
          logging::debug(message);
          break;
        case display_device::Logger::LogLevel::info:
          logging::info(message);
          break;
        case display_device::Logger::LogLevel::warning:
          logging::warning(message);
          break;
        case display_device::Logger::LogLevel::error:
          logging::error(message);
          break;
        case display_device::Logger::LogLevel::fatal:
          logging::fatal(message);
          break;
      }
    });
  }
#endif

  void log_flush() {
    bl::core::get()->flush();
  }

  void print_help(const char *name) {
    std::cout
      << "Usage: "sv << name << " [options] [/path/to/configuration_file] [--cmd]"sv << std::endl
      << "    Any configurable option can be overwritten with: \"name=value\""sv << std::endl
      << std::endl
      << "    Note: The configuration will be created if it doesn't exist."sv << std::endl
      << std::endl
      << "    --help                    | print help"sv << std::endl
      << "    --creds username password | set user credentials for the Web manager"sv << std::endl
      << "    --version                 | print the version of sunshine"sv << std::endl
      << std::endl
      << "    flags"sv << std::endl
      << "        -0 | Read PIN from stdin"sv << std::endl
      << "        -1 | Do not load previously saved state and do retain any state after shutdown"sv << std::endl
      << "           | Effectively starting as if for the first time without overwriting any pairings with your devices"sv << std::endl
      << "        -2 | Force replacement of headers in video stream"sv << std::endl
      << "        -p | Enable/Disable UPnP"sv << std::endl
      << std::endl;
  }

  std::string bracket(const std::string &input) {
    return "["s + input + "]"s;
  }

  std::wstring bracket(const std::wstring &input) {
    return L"["s + input + L"]"s;
  }

}  // namespace logging
