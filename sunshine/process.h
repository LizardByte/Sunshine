//
// Created by loki on 12/14/19.
//

#ifndef SUNSHINE_PROCESS_H
#define SUNSHINE_PROCESS_H

#ifndef __kernel_entry
#define __kernel_entry
#endif

#include <unordered_map>
#include <optional>

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

  std::string name;
  std::string cmd;
  std::string output;
};

class proc_t {
public:
  KITTY_DEFAULT_CONSTR_THROW(proc_t)

  proc_t(
    boost::process::environment &&env,
    std::vector<ctx_t> &&apps) :
    _app_id(-1),
    _env(std::move(env)),
    _apps(std::move(apps)) {}

  int execute(int app_id);

  /**
   * @return _app_id if a process is running, otherwise returns -1
   */
  int running();

  ~proc_t();

  const std::vector<ctx_t> &get_apps() const;
  std::vector<ctx_t> &get_apps();
  
  void terminate();

private:
  int _app_id;

  boost::process::environment _env;
  std::vector<ctx_t> _apps;

  // If no command associated with _app_id, yet it's still running
  bool placebo {};

  boost::process::child _process;
  boost::process::group _process_handle;

  file_t _pipe;
  std::vector<cmd_t>::const_iterator _undo_it;
  std::vector<cmd_t>::const_iterator _undo_begin;
};

void refresh(const std::string &file_name);
std::optional<proc::proc_t> parse(const std::string& file_name);

extern proc_t proc;
}
#endif //SUNSHINE_PROCESS_H
