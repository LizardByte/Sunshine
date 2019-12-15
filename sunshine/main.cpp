//
// Created by loki on 5/30/19.
//

#include <thread>
#include <filesystem>
#include <iostream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "nvhttp.h"
#include "stream.h"
#include "config.h"
#include "process.h"

extern "C" {
#include <rs.h>
}


using namespace std::literals;
namespace pt = boost::property_tree;

std::optional<proc::proc_t> parse(const std::string& file_name) {
  pt::ptree tree;

  try {
    pt::read_json(file_name, tree);

    auto &apps_node = tree.get_child("apps"s);
    auto &env_vars  = tree.get_child("env"s);

    boost::process::environment env = boost::this_process::environment();

    std::unordered_map<std::string, proc::ctx_t> apps;
    for(auto &[_,app_node] : apps_node) {
      proc::ctx_t ctx;

      auto &prep_nodes = app_node.get_child("prep-cmd"s);
      auto name = app_node.get<std::string>("name"s);
      auto output = app_node.get_optional<std::string>("output"s);
      auto cmd = app_node.get<std::string>("cmd"s);

      std::vector<proc::cmd_t> prep_cmds;
      prep_cmds.reserve(prep_nodes.size());
      for(auto &[_, prep_node] : prep_nodes) {
        auto do_cmd = prep_node.get<std::string>("do"s);
        auto undo_cmd = prep_node.get_optional<std::string>("undo"s);

        if(undo_cmd) {
          prep_cmds.emplace_back(std::move(do_cmd), std::move(*undo_cmd));
        }
        else {
          prep_cmds.emplace_back(std::move(do_cmd));
        }
      }

      if(output) {
        ctx.output = std::move(*output);
      }
      ctx.cmd = std::move(cmd);
      ctx.prep_cmds = std::move(prep_cmds);

      apps.emplace(std::move(name), std::move(ctx));
    }

    for(auto &[_,env_var] : env_vars) {
      for(auto &[name,val] : env_var) {
        env[name] = val.get_value<std::string>();
      }
    }

    return proc::proc_t {
      std::move(env), std::move(apps)
    };
  } catch (std::exception &e) {
    std::cout << e.what() << std::endl;
  }

  return std::nullopt;
}

int main(int argc, char *argv[]) {
  auto proc_opt = parse(SUNSHINE_ASSETS_DIR "/apps.json");

  if(!proc_opt) {
    return 7;
  }

  auto &proc = *proc_opt;

  proc.execute("echo-2");
  std::this_thread::sleep_for(50ms);

  proc.execute("echo-1");
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
