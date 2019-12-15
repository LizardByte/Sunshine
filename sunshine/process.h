//
// Created by loki on 12/14/19.
//

#ifndef SUNSHINE_PROCESS_H
#define SUNSHINE_PROCESS_H

#include <unordered_map>

#include <boost/process.hpp>
#include "utility.h"

namespace proc {
using file_t = util::safe_ptr_v2<FILE, int, fclose>;

struct cmd_t {
  cmd_t(std::string &&do_cmd, std::string &&undo_cmd) : do_cmd(std::move(do_cmd)), undo_cmd(std::move(undo_cmd)) {}
  explicit cmd_t(std::string &&do_cmd) : do_cmd(std::move(do_cmd)) {}

  std::string do_cmd;

  // Executed when proc_t has finished running, meant to reverse 'do_cmd' if applicable
  std::string undo_cmd;
};
/*
 * pre_cmds -- guaranteed to be executed unless any of the commands fail.
 * cmd -- Runs indefinitely until:
 *    No session is running and a different set of commands it to be executed
 *    Command exits
 * cmd_output --
 *    empty    -- The output of the commands are appended to the output of sunshine
 *    "null"   -- The output of the commands are discarded
 *    filename -- The output of the commands are appended to filename
 */
struct ctx_t {
  std::vector<cmd_t> prep_cmds;

  std::string cmd;
  std::string output;
};

class proc_t {
public:
  KITTY_DEFAULT_CONSTR(proc_t)

  proc_t(
    boost::process::environment &&env,
    std::unordered_map<std::string, ctx_t> &&name_to_proc) :
    _env(std::move(env)),
    _name_to_proc(std::move(name_to_proc)) {}

  int execute(const std::string &name);
  bool running();

  ~proc_t();

private:
  void _undo_pre_cmd();

  boost::process::environment _env;
  std::unordered_map<std::string, ctx_t> _name_to_proc;

  boost::process::child _process;
  file_t _pipe;
  std::vector<cmd_t>::const_iterator _undo_it;
  std::vector<cmd_t>::const_iterator _undo_begin;
};

}
#endif //SUNSHINE_PROCESS_H
