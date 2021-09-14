//
// Created by loki on 5/30/19.
//

#include "process.h"

#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include <boost/log/attributes/clock.hpp>
#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/severity_logger.hpp>

#include "config.h"
#include "confighttp.h"
#include "httpcommon.h"
#include "main.h"
#include "nvhttp.h"
#include "rtsp.h"
#include "thread_pool.h"
#include "upnp.h"
#include "video.h"

#include "platform/common.h"
extern "C" {
#include <libavutil/log.h>
#include <rs.h>
}

safe::mail_t mail::man;

using namespace std::literals;
namespace bl = boost::log;

util::ThreadPool task_pool;
bl::sources::severity_logger<int> verbose(0); // Dominating output
bl::sources::severity_logger<int> debug(1);   // Follow what is happening
bl::sources::severity_logger<int> info(2);    // Should be informed about
bl::sources::severity_logger<int> warning(3); // Strange events
bl::sources::severity_logger<int> error(4);   // Recoverable errors
bl::sources::severity_logger<int> fatal(5);   // Unrecoverable errors

bool display_cursor = true;

using text_sink = bl::sinks::asynchronous_sink<bl::sinks::text_ostream_backend>;
boost::shared_ptr<text_sink> sink;

struct NoDelete {
  void operator()(void *) {}
};

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", int)

void print_help(const char *name) {
  std::cout
    << "Usage: "sv << name << " [options] [/path/to/configuration_file] [--cmd]"sv << std::endl
    << "    Any configurable option can be overwritten with: \"name=value\""sv << std::endl
    << std::endl
    << "    --help                    | print help"sv << std::endl
    << "    --creds username password | set user credentials for the Web manager" << std::endl
    << std::endl
    << "    flags"sv << std::endl
    << "        -0 | Read PIN from stdin"sv << std::endl
    << "        -1 | Do not load previously saved state and do retain any state after shutdown"sv << std::endl
    << "           | Effectively starting as if for the first time without overwriting any pairings with your devices"sv << std::endl
    << "        -2 | Force replacement of headers in video stream" << std::endl
    << "        -p | Enable/Disable UPnP" << std::endl
    << std::endl;
}

namespace help {
int entry(const char *name, int argc, char *argv[]) {
  print_help(name);
  return 0;
}
} // namespace help

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

namespace gen_creds {
int entry(const char *name, int argc, char *argv[]) {
  if(argc < 2 || argv[0] == "help"sv || argv[1] == "help"sv) {
    print_help(name);
    return 0;
  }

  http::save_user_creds(config::sunshine.credentials_file, argv[0], argv[1]);

  return 0;
}
} // namespace gen_creds

std::map<std::string_view, std::function<int(const char *name, int argc, char **argv)>> cmd_to_func {
  { "creds"sv, gen_creds::entry },
  { "help"sv, help::entry }
};

int main(int argc, char *argv[]) {
  util::TaskPool::task_id_t force_shutdown = nullptr;

  bool shutdown_by_interrupt = false;

  auto exit_guard = util::fail_guard([&shutdown_by_interrupt, &force_shutdown]() {
    if(!shutdown_by_interrupt) {
      return;
    }

    task_pool.cancel(force_shutdown);

    std::cout << "Sunshine exited: Press enter to continue"sv << std::endl;

    std::string _;
    std::getline(std::cin, _);
  });

  mail::man = std::make_shared<safe::mail_raw_t>();

  if(config::parse(argc, argv)) {
    return 0;
  }

  if(config::sunshine.min_log_level >= 1) {
    av_log_set_level(AV_LOG_QUIET);
  }
  else {
    av_log_set_level(AV_LOG_DEBUG);
  }

  sink = boost::make_shared<text_sink>();

  boost::shared_ptr<std::ostream> stream { &std::cout, NoDelete {} };
  sink->locked_backend()->add_stream(stream);
  sink->set_filter(severity >= config::sunshine.min_log_level);

  sink->set_formatter([message = "Message"s, severity = "Severity"s](const bl::record_view &view, bl::formatting_ostream &os) {
    constexpr int DATE_BUFFER_SIZE = 21 + 2 + 1; // Full string plus ": \0"

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

  // Flush after each log record to ensure log file contents on disk isn't stale.
  // This is particularly important when running from a Windows service.
  sink->locked_backend()->auto_flush(true);

  bl::core::get()->add_sink(sink);
  auto fg = util::fail_guard(log_flush);

  if(!config::sunshine.cmd.name.empty()) {
    auto fn = cmd_to_func.find(config::sunshine.cmd.name);
    if(fn == std::end(cmd_to_func)) {
      BOOST_LOG(fatal) << "Unknown command: "sv << config::sunshine.cmd.name;

      BOOST_LOG(info) << "Possible commands:"sv;
      for(auto &[key, _] : cmd_to_func) {
        BOOST_LOG(info) << '\t' << key;
      }

      return 7;
    }

    return fn->second(argv[0], config::sunshine.cmd.argc, config::sunshine.cmd.argv);
  }

  task_pool.start(1);

  // Create signal handler after logging has been initialized
  auto shutdown_event = mail::man->event<bool>(mail::shutdown);
  on_signal(SIGINT, [&shutdown_by_interrupt, &force_shutdown, shutdown_event]() {
    BOOST_LOG(info) << "Interrupt handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      log_flush();
      std::abort();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    shutdown_by_interrupt = true;
    shutdown_event->raise(true);
  });

  on_signal(SIGTERM, [&force_shutdown, shutdown_event]() {
    BOOST_LOG(info) << "Terminate handler called"sv;

    auto task = []() {
      BOOST_LOG(fatal) << "10 seconds passed, yet Sunshine's still running: Forcing shutdown"sv;
      log_flush();
      std::abort();
    };
    force_shutdown = task_pool.pushDelayed(task, 10s).task_id;

    shutdown_event->raise(true);
  });

  proc::refresh(config::stream.file_apps);

  auto deinit_guard = platf::init();
  if(!deinit_guard) {
    return 4;
  }

  reed_solomon_init();
  auto input_deinit_guard = input::init();
  if(video::init()) {
    return 2;
  }
  if(http::init()) {
    return 3;
  }

  std::unique_ptr<platf::deinit_t> mDNS;
  auto sync_mDNS = std::async(std::launch::async, [&mDNS]() {
    mDNS = platf::publish::start();
  });

  std::unique_ptr<platf::deinit_t> upnp_unmap;
  auto sync_upnp = std::async(std::launch::async, [&upnp_unmap]() {
    upnp_unmap = upnp::start();
  });

  //FIXME: Temporary workaround: Simple-Web_server needs to be updated or replaced
  if(shutdown_event->peek()) {
    return 0;
  }

  std::thread httpThread { nvhttp::start };
  std::thread configThread { confighttp::start };

  stream::rtpThread();

  httpThread.join();
  configThread.join();

  task_pool.stop();
  task_pool.join();

  return 0;
}

std::string read_file(const char *path) {
  if(!std::filesystem::exists(path)) {
    return {};
  }

  std::ifstream in(path);

  std::string input;
  std::string base64_cert;

  while(!in.eof()) {
    std::getline(in, input);
    base64_cert += input + '\n';
  }

  return base64_cert;
}

int write_file(const char *path, const std::string_view &contents) {
  std::ofstream out(path);

  if(!out.is_open()) {
    return -1;
  }

  out << contents;

  return 0;
}

std::uint16_t map_port(int port) {
  return (std::uint16_t)((int)config::sunshine.port + port);
}