/**
 * @file src/logging.cpp
 * @brief Definitions for logging related functions.
 */
// standard includes
#include <fstream>
#include <iomanip>
#include <iostream>

// lib includes
#include <boost/core/null_deleter.hpp>
#include <boost/format.hpp>
#include <boost/log/attributes/clock.hpp>
#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/severity_logger.hpp>

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

boost::shared_ptr<boost::log::sinks::asynchronous_sink<boost::log::sinks::text_ostream_backend>> sink;

bl::sources::severity_logger<int> verbose(0);  // Dominating output
bl::sources::severity_logger<int> debug(1);  // Follow what is happening
bl::sources::severity_logger<int> info(2);  // Should be informed about
bl::sources::severity_logger<int> warning(3);  // Strange events
bl::sources::severity_logger<int> error(4);  // Recoverable errors
bl::sources::severity_logger<int> fatal(5);  // Unrecoverable errors
#ifdef SUNSHINE_TESTS
bl::sources::severity_logger<int> tests(10);  // Automatic tests output
#endif

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", int)

namespace logging {
  deinit_t::~deinit_t() {
    deinit();
  }

  void deinit() {
    log_flush();
    bl::core::get()->remove_sink(sink);
    sink.reset();
  }

  void formatter(const boost::log::record_view &view, boost::log::formatting_ostream &os) {
    constexpr const char *message = "Message";
    constexpr const char *severity = "Severity";

    auto log_level = view.attribute_values()[severity].extract<int>().get();

    std::string_view log_type;
    switch (log_level) {
      case 0:
        log_type = "Verbose: "sv;
        break;
      case 1:
        log_type = "Debug: "sv;
        break;
      case 2:
        log_type = "Info: "sv;
        break;
      case 3:
        log_type = "Warning: "sv;
        break;
      case 4:
        log_type = "Error: "sv;
        break;
      case 5:
        log_type = "Fatal: "sv;
        break;
#ifdef SUNSHINE_TESTS
      case 10:
        log_type = "Tests: "sv;
        break;
#endif
    };

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - std::chrono::time_point_cast<std::chrono::seconds>(now)
    );

    auto t = std::chrono::system_clock::to_time_t(now);
    auto lt = *std::localtime(&t);

    os << "["sv << std::put_time(&lt, "%Y-%m-%d %H:%M:%S.") << boost::format("%03u") % ms.count() << "]: "sv
       << log_type << view.attribute_values()[message].extract<std::string>();
  }
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
    if (sink) {
      // Deinitialize the logging system before reinitializing it. This can probably only ever be hit in tests.
      deinit();
    }

#ifndef __ANDROID__
    setup_av_logging(min_log_level);
    setup_libdisplaydevice_logging(min_log_level);
#endif

    sink = boost::make_shared<text_sink>();

#ifndef SUNSHINE_TESTS
    boost::shared_ptr<std::ostream> stream {&std::cout, boost::null_deleter()};
    sink->locked_backend()->add_stream(stream);
#endif

    sink->locked_backend()->add_stream(boost::make_shared<std::ofstream>(log_file));
    sink->set_filter(severity >= min_log_level);
    sink->set_formatter(&formatter);

    // Flush after each log record to ensure log file contents on disk isn't stale.
    // This is particularly important when running from a Windows service.
    sink->locked_backend()->auto_flush(true);

    bl::core::get()->add_sink(sink);

#ifdef __ANDROID__
    auto android_sink = boost::make_shared<sinks::synchronous_sink<android_sink_backend>>();
    bl::core::get()->add_sink(android_sink);
#endif
    return std::make_unique<deinit_t>();
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
      char buffer[1024];

      av_log_format_line(ptr, level, fmt, vl, buffer, sizeof(buffer), &print_prefix);
      if (level <= AV_LOG_ERROR) {
        // We print AV_LOG_FATAL at the error level. FFmpeg prints things as fatal that
        // are expected in some cases, such as lack of codec support or similar things.
        BOOST_LOG(error) << buffer;
      } else if (level <= AV_LOG_WARNING) {
        BOOST_LOG(warning) << buffer;
      } else if (level <= AV_LOG_INFO) {
        BOOST_LOG(info) << buffer;
      } else if (level <= AV_LOG_VERBOSE) {
        // AV_LOG_VERBOSE is less verbose than AV_LOG_DEBUG
        BOOST_LOG(debug) << buffer;
      } else {
        BOOST_LOG(verbose) << buffer;
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
          BOOST_LOG(verbose) << message;
          break;
        case display_device::Logger::LogLevel::debug:
          BOOST_LOG(debug) << message;
          break;
        case display_device::Logger::LogLevel::info:
          BOOST_LOG(info) << message;
          break;
        case display_device::Logger::LogLevel::warning:
          BOOST_LOG(warning) << message;
          break;
        case display_device::Logger::LogLevel::error:
          BOOST_LOG(error) << message;
          break;
        case display_device::Logger::LogLevel::fatal:
          BOOST_LOG(fatal) << message;
          break;
      }
    });
  }
#endif

  void log_flush() {
    if (sink) {
      sink->flush();
    }
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
