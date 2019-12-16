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

extern "C" {
#include <rs.h>
}

using namespace std::literals;
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

  std::thread httpThread { nvhttp::start };
  std::thread rtpThread { stream::rtpThread };

  httpThread.join();
  rtpThread.join();

  return 0;
}
