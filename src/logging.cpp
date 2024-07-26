/**
 * @file src/logging.cpp
 * @brief Definitions for logging related functions.
 */
// standard includes
#include <fstream>
#include <iostream>

// lib includes
#include <boost/core/null_deleter.hpp>
#include <boost/log/attributes/clock.hpp>
#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/severity_logger.hpp>

// local includes
#include "logging.h"

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

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", int)

namespace logging {
  deinit_t::~deinit_t() {
    deinit();
  }

  void
  deinit() {
    log_flush();
    bl::core::get()->remove_sink(sink);
    sink.reset();
  }

  [[nodiscard]] std::unique_ptr<deinit_t>
  init(int min_log_level, const std::string &log_file) {
    if (sink) {
      // Deinitialize the logging system before reinitializing it. This can probably only ever be hit in tests.
      deinit();
    }

    setup_av_logging(min_log_level);

    sink = boost::make_shared<text_sink>();

    boost::shared_ptr<std::ostream> stream { &std::cout, boost::null_deleter() };
    sink->locked_backend()->add_stream(stream);
    sink->locked_backend()->add_stream(boost::make_shared<std::ofstream>(log_file));
    sink->set_filter(severity >= min_log_level);

    sink->set_formatter([](const bl::record_view &view, bl::formatting_ostream &os) {
      constexpr const char *message = "Message";
      constexpr const char *severity = "Severity";
      constexpr int DATE_BUFFER_SIZE = 21 + 2 + 1;  // Full string plus ": \0"

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
      };

      char _date[DATE_BUFFER_SIZE];
      std::time_t t = std::time(nullptr);
      strftime(_date, DATE_BUFFER_SIZE, "[%Y:%m:%d:%H:%M:%S]: ", std::localtime(&t));

      os << _date << log_type << view.attribute_values()[message].extract<std::string>();
    });

    // Flush after each log record to ensure log file contents on disk isn't stale.
    // This is particularly important when running from a Windows service.
    sink->locked_backend()->auto_flush(true);

    bl::core::get()->add_sink(sink);
    return std::make_unique<deinit_t>();
  }

  void
  setup_av_logging(int min_log_level) {
    if (min_log_level >= 1) {
      av_log_set_level(AV_LOG_QUIET);
    }
    else {
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
      }
      else if (level <= AV_LOG_WARNING) {
        BOOST_LOG(warning) << buffer;
      }
      else if (level <= AV_LOG_INFO) {
        BOOST_LOG(info) << buffer;
      }
      else if (level <= AV_LOG_VERBOSE) {
        // AV_LOG_VERBOSE is less verbose than AV_LOG_DEBUG
        BOOST_LOG(debug) << buffer;
      }
      else {
        BOOST_LOG(verbose) << buffer;
      }
    });
  }

  void
  log_flush() {
    if (sink) {
      sink->flush();
    }
  }

  void
  print_help(const char *name) {
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

  std::string
  bracket(const std::string &input) {
    return bracket(std::string_view(input));
  }

  std::string
  bracket(const std::string_view &input) {
    return "["s + std::string(input) + "]"s;
  }

  std::wstring
  bracket(const std::wstring &input) {
    return bracket(std::wstring_view(input));
  }

  std::wstring
  bracket(const std::wstring_view &input) {
    return L"["s + std::wstring(input) + L"]"s;
  }

}  // namespace logging
