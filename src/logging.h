/**
 * @file src/logging.h
 * @brief Logging header file for the Sunshine application.
 */

// macros
#pragma once

// lib includes
#include <boost/log/common.hpp>
#include <boost/log/sinks.hpp>

using text_sink = boost::log::sinks::asynchronous_sink<boost::log::sinks::text_ostream_backend>;

extern boost::log::sources::severity_logger<int> verbose;
extern boost::log::sources::severity_logger<int> debug;
extern boost::log::sources::severity_logger<int> info;
extern boost::log::sources::severity_logger<int> warning;
extern boost::log::sources::severity_logger<int> error;
extern boost::log::sources::severity_logger<int> fatal;

namespace logging {
  class deinit_t {
  public:
    ~deinit_t();
  };

  void
  deinit();
  [[nodiscard]] std::unique_ptr<deinit_t>
  init(int min_log_level, const std::string &log_file);
  void
  setup_av_logging(int min_log_level);
  void
  log_flush();
  void
  print_help(const char *name);
}  // namespace logging
