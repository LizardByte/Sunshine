//
// Created by loki on 12/14/19.
//

#include <vector>
#include <string>
#include <iostream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "process.h"
#include "config.h"
#include "utility.h"
#include "platform/common.h"

namespace proc {
using namespace std::literals;
namespace bp = boost::process;
namespace pt = boost::property_tree;

proc_t proc;

template<class Rep, class Period>
void process_end(bp::child &proc, bp::group &proc_handle, const std::chrono::duration<Rep, Period>& rel_time) {
  if(!proc.running()) {
    return;
  }

  std::cout << "Force termination Child-Process"sv << std::endl;
  proc_handle.terminate();

  // avoid zombie process
  proc.wait();
}

int exe(const std::string &cmd, bp::environment &env, file_t &file, std::error_code &ec) {
  if(!file) {
    return bp::system(cmd, env, bp::std_out > bp::null, bp::std_err > bp::null, ec);
  }

  return bp::system(cmd, env, bp::std_out > file.get(), bp::std_err > file.get(), ec);
}

int proc_t::execute(const std::string &name) {
  auto it = _name_to_proc.find(name);

  std::cout << "Ensure clean slate"sv << std::endl;
  // Ensure starting from a clean slate
  _undo_pre_cmd();
  std::cout << "Clean slate"sv << std::endl;

  if(it == std::end(_name_to_proc)) {
    std::cout << "Error: Couldn't find ["sv << name << ']' << std::endl;
    return 404;
  }

  auto &proc = it->second;

  _undo_begin = std::begin(proc.prep_cmds);
  _undo_it = _undo_begin;

  if(!proc.output.empty() && proc.output != "null"sv) {
    _pipe.reset(fopen(proc.output.c_str(), "a"));
  }

  std::error_code ec;
  //Executed when returning from function
  auto fg = util::fail_guard([&]() {
    _undo_pre_cmd();
  });

  for(; _undo_it != std::end(proc.prep_cmds); ++_undo_it) {
    auto &cmd = _undo_it->do_cmd;

    std::cout << "Executing: ["sv << cmd << ']' << std::endl;
    auto ret = exe(cmd, _env, _pipe, ec);

    if(ec) {
      std::cout << "Error: System: "sv << ec.message() << std::endl;
      return -1;
    }

    if(ret != 0) {
      std::cout << "Error: return code ["sv << ret << ']';
      return -1;
    }
  }

  std::cout << "Starting ["sv << proc.cmd << ']' << std::endl;
  if(proc.output.empty() || proc.output == "null"sv) {
    _process = bp::child(_process_handle, proc.cmd, _env, bp::std_out > bp::null, bp::std_err > bp::null, ec);
  }
  else {
    _process = bp::child(_process_handle, proc.cmd, _env, bp::std_out > _pipe.get(), bp::std_err > _pipe.get(), ec);
  }

  if(ec) {
    std::cout << "Error: System: "sv << ec.message() << std::endl;
    return -1;
  }

  fg.disable();

  return 0;
}

bool proc_t::running() {
  return _process.running();
}

void proc_t::_undo_pre_cmd() {
  std::error_code ec;

  // Ensure child process is terminated
  process_end(_process, _process_handle, 10s);

  if(ec) {
    std::cout << "FATAL Error: System: "sv << ec.message() << std::endl;
    std::abort();
  }

  for(;_undo_it != _undo_begin; --_undo_it) {
    auto &cmd = (_undo_it - 1)->undo_cmd;

    if(cmd.empty()) {
      continue;
    }

    std::cout << "Executing: ["sv << cmd << ']' << std::endl;

    auto ret = exe(cmd, _env, _pipe, ec);

    if(ec) {
      std::cout << "FATAL Error: System: "sv << ec.message() << std::endl;
      std::abort();
    }

    if(ret != 0) {
      std::cout << "FATAL Error: return code ["sv << ret << ']';
      std::abort();
    }
  }

  _pipe.reset();
}

const std::unordered_map<std::string, ctx_t> &proc_t::get_apps() const {
  return _name_to_proc;
}

proc_t::~proc_t() {
  _undo_pre_cmd();
}

std::string_view::iterator find_match(std::string_view::iterator begin, std::string_view::iterator end) {
  int stack = 0;

  --begin;
  do {
    ++begin;
    switch(*begin) {
      case '(': ++stack;
      break;
      case ')': --stack;
    }
  } while(begin != end && stack != 0);

  if(begin == end) {
    throw std::out_of_range("Missing closing bracket \')\'");
  }
  return begin;
}

std::string parse_env_val(bp::native_environment &env, const std::string_view &val_raw) {
  auto pos = std::begin(val_raw);
  auto dollar = std::find(pos, std::end(val_raw), '$');

  std::stringstream ss;

  while(dollar != std::end(val_raw)) {
    auto next = dollar + 1;
    if(next != std::end(val_raw)) {
      switch(*next) {
        case '(': {
          ss.write(pos, (dollar - pos));
          auto var_begin = next + 1;
          auto var_end   = find_match(next, std::end(val_raw));

          ss << env[std::string { var_begin, var_end }].to_string();

          pos = var_end + 1;
          next = var_end;

          break;
        }
        case '$':
          ss.write(pos, (next - pos));
          pos = next + 1;
          ++next;
          break;
      }

      dollar = std::find(next, std::end(val_raw), '$');
    }
    else {
      dollar = next;
    }
  }

  ss.write(pos, (dollar - pos));

  return ss.str();
}

std::optional<proc::proc_t> parse(const std::string& file_name) {
  pt::ptree tree;

  try {
    pt::read_json(file_name, tree);

    auto &apps_node = tree.get_child("apps"s);
    auto &env_vars  = tree.get_child("env"s);

    auto this_env = boost::this_process::environment();

    std::unordered_map<std::string, proc::ctx_t> apps;
    for(auto &[_,app_node] : apps_node) {
      proc::ctx_t ctx;

      auto &prep_nodes = app_node.get_child("prep-cmd"s);
      auto output = app_node.get_optional<std::string>("output"s);
      auto name = parse_env_val(this_env, app_node.get<std::string>("name"s));
      auto cmd = app_node.get_optional<std::string>("cmd"s);

      std::vector<proc::cmd_t> prep_cmds;
      prep_cmds.reserve(prep_nodes.size());
      for(auto &[_, prep_node] : prep_nodes) {
        auto do_cmd = parse_env_val(this_env, prep_node.get<std::string>("do"s));
        auto undo_cmd = prep_node.get_optional<std::string>("undo"s);

        if(undo_cmd) {
          prep_cmds.emplace_back(std::move(do_cmd), parse_env_val(this_env, *undo_cmd));
        }
        else {
          prep_cmds.emplace_back(std::move(do_cmd));
        }
      }

      if(output) {
        ctx.output = parse_env_val(this_env, *output);
      }

      if(cmd) {
        ctx.cmd = parse_env_val(this_env, *cmd);
      }
      else {
        ctx.cmd = "sh -c \"while true; do sleep 10000; done;\"";
      }

      ctx.prep_cmds = std::move(prep_cmds);

      apps.emplace(std::move(name), std::move(ctx));
    }

    bp::environment env = boost::this_process::environment();
    for(auto &[name,val] : env_vars) {
      this_env[name] = parse_env_val(this_env, val.get_value<std::string>());
    }

    return proc::proc_t {
      std::move(this_env), std::move(apps)
    };
  } catch (std::exception &e) {
    std::cout << e.what() << std::endl;
  }

  return std::nullopt;
}

void refresh(const std::string &file_name) {
  auto proc_opt = proc::parse(file_name);

  if(proc_opt) {
    proc = std::move(*proc_opt);
  }
}
}