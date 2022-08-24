//
// Created by loki on 12/14/19.
//

#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "process.h"

#include <filesystem>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "main.h"
#include "utility.h"

namespace proc {
using namespace std::literals;
namespace bp = boost::process;
namespace pt = boost::property_tree;

proc_t proc;

void process_end(bp::child &proc, bp::group &proc_handle) {
  if(!proc.running()) {
    return;
  }

  BOOST_LOG(debug) << "Force termination Child-Process"sv;
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

int proc_t::execute(int app_id) {
  if(!running() && _app_id != -1) {
    // previous process exited on it's own, reset _process_handle
    _process_handle = bp::group();

    _app_id = -1;
  }

  if(app_id < 0 || app_id >= _apps.size()) {
    BOOST_LOG(error) << "Couldn't find app with ID ["sv << app_id << ']';

    return 404;
  }

  // Ensure starting from a clean slate
  terminate();

  _app_id    = app_id;
  auto &proc = _apps[app_id];

  _undo_begin = std::begin(proc.prep_cmds);
  _undo_it    = _undo_begin;

  if(!proc.output.empty() && proc.output != "null"sv) {
    _pipe.reset(fopen(proc.output.c_str(), "a"));
  }

  std::error_code ec;
  //Executed when returning from function
  auto fg = util::fail_guard([&]() {
    terminate();
  });

  for(; _undo_it != std::end(proc.prep_cmds); ++_undo_it) {
    auto &cmd = _undo_it->do_cmd;

    BOOST_LOG(info) << "Executing: ["sv << cmd << ']';
    auto ret = exe(cmd, _env, _pipe, ec);

    if(ec) {
      BOOST_LOG(error) << "Couldn't run ["sv << cmd << "]: System: "sv << ec.message();
      return -1;
    }

    if(ret != 0) {
      BOOST_LOG(error) << '[' << cmd << "] failed with code ["sv << ret << ']';
      return -1;
    }
  }

  for(auto &cmd : proc.detached) {
    BOOST_LOG(info) << "Spawning ["sv << cmd << ']';
    if(proc.output.empty() || proc.output == "null"sv) {
      bp::spawn(cmd, _env, bp::std_out > bp::null, bp::std_err > bp::null, ec);
    }
    else {
      bp::spawn(cmd, _env, bp::std_out > _pipe.get(), bp::std_err > _pipe.get(), ec);
    }

    if(ec) {
      BOOST_LOG(warning) << "Couldn't spawn ["sv << cmd << "]: System: "sv << ec.message();
    }
  }

  if(proc.cmd.empty()) {
    BOOST_LOG(debug) << "Executing [Desktop]"sv;
    placebo = true;
  }
  else {
    boost::filesystem::path working_dir = proc.working_dir.empty() ?
                                            boost::filesystem::path(proc.cmd).parent_path() :
                                            boost::filesystem::path(proc.working_dir);
    if(proc.output.empty() || proc.output == "null"sv) {
      BOOST_LOG(info) << "Executing: ["sv << proc.cmd << ']';
      _process = bp::child(_process_handle, proc.cmd, _env, bp::start_dir(working_dir), bp::std_out > bp::null, bp::std_err > bp::null, ec);
    }
    else {
      BOOST_LOG(info) << "Executing: ["sv << proc.cmd << ']';
      _process = bp::child(_process_handle, proc.cmd, _env, bp::start_dir(working_dir), bp::std_out > _pipe.get(), bp::std_err > _pipe.get(), ec);
    }
  }

  if(ec) {
    BOOST_LOG(warning) << "Couldn't run ["sv << proc.cmd << "]: System: "sv << ec.message();
    return -1;
  }

  fg.disable();

  return 0;
}

int proc_t::running() {
  if(placebo || _process.running()) {
    return _app_id;
  }

  return -1;
}

void proc_t::terminate() {
  std::error_code ec;

  // Ensure child process is terminated
  placebo = false;
  process_end(_process, _process_handle);
  _app_id = -1;

  if(ec) {
    BOOST_LOG(fatal) << "System: "sv << ec.message();
    log_flush();
    std::abort();
  }

  for(; _undo_it != _undo_begin; --_undo_it) {
    auto &cmd = (_undo_it - 1)->undo_cmd;

    if(cmd.empty()) {
      continue;
    }

    BOOST_LOG(debug) << "Executing: ["sv << cmd << ']';

    auto ret = exe(cmd, _env, _pipe, ec);

    if(ec) {
      BOOST_LOG(fatal) << "System: "sv << ec.message();
      log_flush();
      std::abort();
    }

    if(ret != 0) {
      BOOST_LOG(fatal) << "Return code ["sv << ret << ']';
      log_flush();
      std::abort();
    }
  }

  _pipe.reset();
}

const std::vector<ctx_t> &proc_t::get_apps() const {
  return _apps;
}
std::vector<ctx_t> &proc_t::get_apps() {
  return _apps;
}

/// Gets application image from application list.
/// Returns default image if image configuration is not set.
/// returns http content-type header compatible image type
std::string proc_t::get_app_image(int app_id) {
  auto app_index = app_id - 1;
  if(app_index < 0 || app_index >= _apps.size()) {
    BOOST_LOG(error) << "Couldn't find app with ID ["sv << app_id << ']';
    return SUNSHINE_ASSETS_DIR "/box.png";
  }

  auto app_image_path = _apps[app_index].image_path;
  if(app_image_path.empty()) {
    return SUNSHINE_ASSETS_DIR "/box.png";
  }

  auto image_extension = std::filesystem::path(app_image_path).extension().string();
  boost::to_lower(image_extension);

  std::error_code code;
  if(!std::filesystem::exists(app_image_path, code) || image_extension != ".png") {
    return SUNSHINE_ASSETS_DIR "/box.png";
  }

  // return only "content-type" http header compatible image type.
  return app_image_path;
}

proc_t::~proc_t() {
  terminate();
}

std::string_view::iterator find_match(std::string_view::iterator begin, std::string_view::iterator end) {
  int stack = 0;

  --begin;
  do {
    ++begin;
    switch(*begin) {
    case '(':
      ++stack;
      break;
    case ')':
      --stack;
    }
  } while(begin != end && stack != 0);

  if(begin == end) {
    throw std::out_of_range("Missing closing bracket \')\'");
  }
  return begin;
}

std::string parse_env_val(bp::native_environment &env, const std::string_view &val_raw) {
  auto pos    = std::begin(val_raw);
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

        pos  = var_end + 1;
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

std::optional<proc::proc_t> parse(const std::string &file_name) {
  pt::ptree tree;

  try {
    pt::read_json(file_name, tree);

    auto &apps_node = tree.get_child("apps"s);
    auto &env_vars  = tree.get_child("env"s);

    auto this_env = boost::this_process::environment();

    for(auto &[name, val] : env_vars) {
      this_env[name] = parse_env_val(this_env, val.get_value<std::string>());
    }

    std::vector<proc::ctx_t> apps;
    for(auto &[_, app_node] : apps_node) {
      proc::ctx_t ctx;

      auto prep_nodes_opt     = app_node.get_child_optional("prep-cmd"s);
      auto detached_nodes_opt = app_node.get_child_optional("detached"s);
      auto output             = app_node.get_optional<std::string>("output"s);
      auto name               = parse_env_val(this_env, app_node.get<std::string>("name"s));
      auto cmd                = app_node.get_optional<std::string>("cmd"s);
      auto image_path         = app_node.get_optional<std::string>("image-path"s);
      auto working_dir        = app_node.get_optional<std::string>("working-dir"s);

      std::vector<proc::cmd_t> prep_cmds;
      if(prep_nodes_opt) {
        auto &prep_nodes = *prep_nodes_opt;

        prep_cmds.reserve(prep_nodes.size());
        for(auto &[_, prep_node] : prep_nodes) {
          auto do_cmd   = parse_env_val(this_env, prep_node.get<std::string>("do"s));
          auto undo_cmd = prep_node.get_optional<std::string>("undo"s);

          if(undo_cmd) {
            prep_cmds.emplace_back(std::move(do_cmd), parse_env_val(this_env, *undo_cmd));
          }
          else {
            prep_cmds.emplace_back(std::move(do_cmd));
          }
        }
      }

      std::vector<std::string> detached;
      if(detached_nodes_opt) {
        auto &detached_nodes = *detached_nodes_opt;

        detached.reserve(detached_nodes.size());
        for(auto &[_, detached_val] : detached_nodes) {
          detached.emplace_back(parse_env_val(this_env, detached_val.get_value<std::string>()));
        }
      }

      if(output) {
        ctx.output = parse_env_val(this_env, *output);
      }

      if(cmd) {
        ctx.cmd = parse_env_val(this_env, *cmd);
      }

      if(working_dir) {
        ctx.working_dir = parse_env_val(this_env, *working_dir);
      }

      if(image_path) {
        ctx.image_path = parse_env_val(this_env, *image_path);
      }

      ctx.name      = std::move(name);
      ctx.prep_cmds = std::move(prep_cmds);
      ctx.detached  = std::move(detached);

      apps.emplace_back(std::move(ctx));
    }

    return proc::proc_t {
      std::move(this_env), std::move(apps)
    };
  }
  catch(std::exception &e) {
    BOOST_LOG(error) << e.what();
  }

  return std::nullopt;
}

void refresh(const std::string &file_name) {
  auto proc_opt = proc::parse(file_name);

  if(proc_opt) {
    {
      proc::ctx_t ctx;
      ctx.name = "Desktop"s;
      proc_opt->get_apps().emplace(std::begin(proc_opt->get_apps()), std::move(ctx));
    }
    proc = std::move(*proc_opt);
  }
}
} // namespace proc
