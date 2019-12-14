//
// Created by loki on 12/14/19.
//

#include <vector>
#include <string>
#include <iostream>

#include "process.h"
#include "config.h"
#include "utility.h"
#include "platform/common.h"

namespace proc {
using namespace std::literals;
namespace bp = boost::process;

template<class Rep, class Period>
void process_end(bp::child &proc, const std::chrono::duration<Rep, Period>& rel_time) {
  if(!proc.running()) {
    return;
  }

  platf::interrupt_process((std::uint64_t)proc.native_handle());

  // Force termination if it takes too long
  if(!proc.wait_for(rel_time)) {
    proc.terminate();
  }
}

int exe(const std::string &cmd, bp::environment &env, file_t &file, std::error_code &ec) {
  if(cmd.empty() || cmd == "null"sv) {
    return bp::system(cmd, env, bp::std_out > bp::null, bp::std_err > bp::null, ec);
  }

  return bp::system(cmd, env, bp::std_out > file.get(), bp::std_err > file.get(), ec);
}

int proc_t::execute(const std::string &name) {
  auto it = _name_to_proc.find(name);

  // Ensure starting from a clean slate
  _undo_pre_cmd();

  if(it == std::end(_name_to_proc)) {
    std::cout << "Error: Couldn't find ["sv << name << ']' << std::endl;
    return 404;
  }

  auto &proc = it->second;

  _undo_begin = std::begin(proc.pre_cmds);
  _undo_it = _undo_begin;

  if(!proc.cmd_output.empty() && proc.cmd_output != "null"sv) {
    _pipe.reset(fopen(proc.cmd_output.c_str(), "a"));
  }

  std::error_code ec;
  //Executed when returning from function
  auto fg = util::fail_guard([&]() {
    _undo_pre_cmd();
  });

  for(; _undo_it != std::end(proc.pre_cmds); ++_undo_it) {
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
  if(proc.cmd_output.empty() || proc.cmd_output == "null"sv) {
    _process = bp::child(proc.cmd, _env, bp::std_out > bp::null, bp::std_err > bp::null, ec);
  }
  else {
    _process = bp::child(proc.cmd, _env, bp::std_out > proc.cmd_output, bp::std_err > proc.cmd_output, ec);
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
  process_end(_process, 10s);

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
proc_t::~proc_t() {
  _undo_pre_cmd();
}
}