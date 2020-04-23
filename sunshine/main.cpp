//
// Created by loki on 5/30/19.
//

#include "process.h"

#include <thread>
#include <iostream>
#include <csignal>

#include <boost/log/common.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/attributes/clock.hpp>

#include "video.h"
#include "input.h"
#include "nvhttp.h"
#include "rtsp.h"
#include "config.h"
#include "thread_pool.h"

#include "platform/common.h"
extern "C" {
#include <rs.h>
#include <libavutil/log.h>
}

using namespace std::literals;
namespace bl = boost::log;

util::ThreadPool task_pool;
bl::sources::severity_logger<int> verbose(0); // Dominating output
bl::sources::severity_logger<int> debug(1);   // Follow what is happening
bl::sources::severity_logger<int> info(2);    // Should be informed about
bl::sources::severity_logger<int> warning(3); // Strange events
bl::sources::severity_logger<int> error(4);   // Recoverable errors
bl::sources::severity_logger<int> fatal(5);   // Unrecoverable errors

bool display_cursor;

using text_sink = bl::sinks::asynchronous_sink<bl::sinks::text_ostream_backend>;
boost::shared_ptr<text_sink> sink;

struct NoDelete {
  void operator()(void *) {}
};

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", int)

void log_flush() {
  sink->flush();
}

std::map<int, std::function<void()>> signal_handlers;
void on_signal_forwarder(int sig) {
  signal_handlers.at(sig)();
}

template<class FN>
void on_signal(int sig, FN &&fn) {
  signal_handlers.emplace(sig, std::forward<FN>(fn));

  std::signal(sig, on_signal_forwarder);
}

int main(int argc, char *argv[]) {
  if(config::parse(argc, argv)) {
    return 0;
  }

  if(config::sunshine.min_log_level >= 2) {
    av_log_set_level(AV_LOG_QUIET);
  }

  sink = boost::make_shared<text_sink>();

  boost::shared_ptr<std::ostream> stream { &std::cout, NoDelete {} };
  sink->locked_backend()->add_stream(stream);
  sink->set_filter(severity >= config::sunshine.min_log_level);

  sink->set_formatter([message="Message"s, severity="Severity"s](const bl::record_view &view, bl::formatting_ostream &os) {
    constexpr int DATE_BUFFER_SIZE = 21 +2 +1; // Full string plus ": \0"

    auto log_level = view.attribute_values()[severity].extract<int>().get();

    std::string_view log_type;
    switch(log_level) {
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

  bl::core::get()->add_sink(sink);
  auto fg = util::fail_guard(log_flush);

  // Create signal handler after logging has been initialized
  auto shutdown_event = std::make_shared<safe::event_t<bool>>();
  on_signal(SIGINT, [shutdown_event]() {
    BOOST_LOG(info) << "Interrupt handler called"sv;
    shutdown_event->raise(true);
  });

  auto proc_opt = proc::parse(config::stream.file_apps);
  if(!proc_opt) {
    return 7;
  }

  {
    proc::ctx_t ctx;
    ctx.name = "Desktop"s;
    proc_opt->get_apps().emplace(std::begin(proc_opt->get_apps()), std::move(ctx));
  }

  proc::proc = std::move(*proc_opt);

  auto deinit_guard = platf::init();
  input::init();
  reed_solomon_init();
  if(video::init()) {
    return 2;
  }

  task_pool.start(1);

  std::thread httpThread { nvhttp::start, shutdown_event };
  stream::rtpThread(shutdown_event);

  httpThread.join();

  return 0;
}
