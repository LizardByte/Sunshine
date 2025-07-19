#pragma once

#ifdef SUNSHINE_WGC_HELPER_BUILD
// Standalone logging for WGC helper
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/trivial.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <ostream>

// Severity levels (must match those in sunshine_wgc_capture.cpp)
enum severity_level {
  trace,
  debug,
  info,
  warning,
  error,
  fatal
};

// Stream operator for severity_level
inline std::ostream& operator<<(std::ostream& strm, severity_level level) {
  static const char* strings[] = {
    "TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL"
  };
  if (static_cast<std::size_t>(level) < sizeof(strings) / sizeof(*strings))
    strm << strings[level];
  else
    strm << static_cast<int>(level);
  return strm;
}

// Global logger instance
enum severity_level;
extern boost::log::sources::severity_logger<severity_level> g_logger;

#ifdef BOOST_LOG
#undef BOOST_LOG
#endif
#define BOOST_LOG(level) BOOST_LOG_SEV(g_logger, level)

#else
// Use main process logging
#include "src/logging.h"
#endif
