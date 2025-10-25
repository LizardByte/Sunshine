/**
 * @file src/logging_android.cpp
 * @brief Android-specific logging implementation.
 */
#ifdef __ANDROID__

  // lib includes
  #include <boost/log/common.hpp>
  #include <boost/log/expressions.hpp>
  #include <boost/log/sinks.hpp>

  // Android includes
  #include <android/log.h>

  // local includes
  #include "logging.h"
  #include "logging_android.h"

using namespace std::literals;

namespace bl = boost::log;
namespace sinks = boost::log::sinks;
namespace expr = boost::log::expressions;

namespace logging {
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

  void setup_android_logging() {
    auto android_sink = boost::make_shared<sinks::synchronous_sink<android_sink_backend>>();
    bl::core::get()->add_sink(android_sink);
  }
}  // namespace logging

#endif
