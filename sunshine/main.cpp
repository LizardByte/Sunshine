//
// Created by loki on 5/30/19.
//

#include <thread>
#include <filesystem>
#include <iostream>

#include "nvhttp.h"
#include "stream.h"
#include "config.h"
#include "process.h"
#include "thread_pool.h"

extern "C" {
#include <rs.h>
}

#include "platform/common.h"
using namespace std::literals;

util::ThreadPool task_pool;
bool display_cursor;

int main(int argc, char *argv[]) {
  if(argc > 1) {
    if(!std::filesystem::exists(argv[1])) {
      std::cout << "Error: Couldn't find configuration file ["sv << argv[1] << ']' << std::endl;
      return 7;
    }

    config::parse_file(argv[1]);
  }

  auto proc_opt = proc::parse(config::stream.file_apps);
  if(!proc_opt) {
    return 7;
  }
  proc::proc = std::move(*proc_opt);

  reed_solomon_init();

  task_pool.start(1);

  std::thread httpThread { nvhttp::start };

  stream::rtpThread();
  httpThread.join();

  return 0;
}
