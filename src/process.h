// Created by loki on 12/14/19.

#ifndef SUNSHINE_PROCESS_H
#define SUNSHINE_PROCESS_H

#ifndef __kernel_entry
#define __kernel_entry
#endif

#include <optional>
#include <unordered_map>

#include <boost/json.hpp>
#include <boost/process.hpp>

#include "utility.h"

namespace proc {
namespace json = boost::json;
using file_t   = util::safe_ptr_v2<FILE, int, fclose>;

struct cmd_t {
  cmd_t(std::string &&do_cmd, std::string &&undo_cmd) : do_cmd(std::move(do_cmd)), undo_cmd(std::move(undo_cmd)) {}
  explicit cmd_t(std::string &&do_cmd) : do_cmd(std::move(do_cmd)) {}

  std::string do_cmd;

  // Executed when proc_t has finished running, meant to reverse 'do_cmd' if applicable
  std::string undo_cmd;
};
/*
 * pre_cmds -- guaranteed to be executed unless any of the commands fail.
 * detached -- commands detached from Sunshine
 * cmd -- Runs indefinitely until:
 *    No session is running and a different set of commands it to be executed
 *    Command exits
 * working_dir -- the process working directory. This is required for some games to run properly.
 * cmd_output --
 *    empty    -- The output of the commands are appended to the output of sunshine
 *    "null"   -- The output of the commands are discarded
 *    filename -- The output of the commands are appended to filename
 */
struct ctx_t {
  std::vector<cmd_t> prep_cmds;

  /**
   * Some applications, such as Steam,
   * either exit quickly, or keep running indefinitely.
   * Steam.exe is one such application.
   * That is why some applications need be run and forgotten about
   */
  std::vector<std::string> detached;

  int id;
  std::string name;
  std::string cmd;
  std::string working_dir;
  std::string output;
  std::string image_path;

  friend void tag_invoke(json::value_from_tag, json::value &jv, ctx_t const &c) {
    json::array prep_commands;
    for(auto &prep_cmd : c.prep_cmds) {
      json::object cmdJson;
      cmdJson["do"]   = prep_cmd.do_cmd;
      cmdJson["undo"] = prep_cmd.undo_cmd;
      prep_commands.push_back(cmdJson);
    }

    jv = {
      { "id", c.id },
      { "name", c.name },
      { "image-path", c.image_path },
      { "cmd", c.cmd },
      { "detached", json::array(c.detached.begin(), c.detached.end()) },
      { "prep-cmd", prep_commands },
      { "output", c.output },
      { "working-dir", c.working_dir }
    };
  }

  friend ctx_t tag_invoke(json::value_to_tag<ctx_t>, json::value const &jv) {
    json::object const &obj = jv.as_object();

    std::vector<cmd_t> prep_cmds;
    if (obj.contains("prep-cmd") && obj.at("prep-cmd").is_array()) {
      for(auto &prep_cmd : obj.at("prep-cmd").as_array()) {

        std::string &&doCmd = "", undoCmd;
        if(prep_cmd.at("do").is_string()) doCmd = prep_cmd.at("do").as_string();
        if(prep_cmd.at("undo").is_string()) undoCmd = prep_cmd.at("undo").as_string();

        prep_cmds.emplace_back(std::move(doCmd), std::move(undoCmd));
      }
    }

    std::vector<std::string> detached;
    if (obj.contains("detached") && obj.at("detached").is_array()) {
      for(auto val : obj.at("detached").as_array()) {
        detached.emplace_back(val.as_string());
      }
    }

    int id = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (obj.contains("id") && obj.at("id").is_int64()) {
      id = obj.at("id").as_int64();
    }

    std::string cmd, working_dir, output, image_path;
    if (obj.contains("cmd") && obj.at("cmd").is_string()) {
      cmd = obj.at("cmd").as_string();
    }
    if (obj.contains("working-dir") && obj.at("working-dir").is_string()) {
      working_dir = obj.at("working-dir").as_string();
    }
    if (obj.contains("output") && obj.at("output").is_string()) {
      output = obj.at("output").as_string();
    }
    if (obj.contains("image-path") && obj.at("image-path").is_string()) {
      image_path = obj.at("image-path").as_string();
    }
    return ctx_t {
      prep_cmds,
      detached,
      id,
      json::value_to<std::string>(obj.at("name")),
      cmd,
      working_dir,
      output,
      image_path
    };
  }
};

class proc_t {
public:
  KITTY_DEFAULT_CONSTR_MOVE_THROW(proc_t)

  proc_t(
    boost::process::environment &&env,
    std::vector<ctx_t> &&apps) : _app_id(-1),
                                 _env(std::move(env)),
                                 _apps(std::move(apps)) {}

  int execute(int app_id);

  /**
   * @return _app_id if a process is running, otherwise returns -1
   */
  int running();

  ~proc_t();

  const std::vector<ctx_t> &get_apps() const;
  std::string get_app_image(int app_id);

  void add_app(int id, ctx_t app);
  bool remove_app(int app_id);

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

bool save(const std::string &file_name);
void parse(const std::string &file_name);

extern proc_t proc;
} // namespace proc
#endif // SUNSHINE_PROCESS_H
