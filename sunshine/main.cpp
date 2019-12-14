//
// Created by loki on 5/30/19.
//

#include <thread>
#include <filesystem>
#include <iostream>

#include "nvhttp.h"
#include "stream.h"

extern "C" {
#include <rs.h>
}

#include "config.h"

#include "process.h"

using namespace std::literals;
int main(int argc, char *argv[]) {
  std::vector<proc::cmd_t> pre_cmds {
    { "echo pre-1", "echo post-1" },
    { "echo pre-2", "" },
    { "echo pre-3", "echo post-3" }
  };

  std::unordered_map<std::string, proc::ctx_t> map {
    { "echo", { std::move(pre_cmds), R"(echo \"middle\")", "output.txt" } }
  };

  boost::process::environment env = boost::this_process::environment();
  proc::proc_t proc(std::move(env), std::move(map));

  proc.execute("echo"s);

  std::this_thread::sleep_for(50ms);

  proc.execute("echo"s);

  std::this_thread::sleep_for(50ms);
  return proc.running();

  if(argc > 1) {
    if(!std::filesystem::exists(argv[1])) {
      std::cout << "Error: Couldn't find configuration file ["sv << argv[1] << ']' << std::endl;
      return 7;
    }

    config::parse_file(argv[1]);
  }

  reed_solomon_init();

  std::thread httpThread { nvhttp::start };
  std::thread rtpThread { stream::rtpThread };

  httpThread.join();
  rtpThread.join();

  return 0;
}
