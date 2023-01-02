// Created by loki on 12/14/19.

#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "process.h"

#include <filesystem>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/json.hpp>
#include <boost/program_options/parsers.hpp>

#include "main.h"
#include "platform/common.h"
#include "utility.h"

#ifdef _WIN32
// _SH constants for _wfsopen()
#include <share.h>
#endif

namespace proc {
using namespace std::literals;
namespace bp   = boost::process;
namespace json = boost::json;

proc_t proc;

void process_end(bp::child &process, bp::group &proc_handle) {
  if(!process.running()) {
    return;
  }

  BOOST_LOG(debug) << "Force termination Child-Process"sv;
  proc_handle.terminate();

  // avoid zombie process
  process.wait();
}

int exe_with_full_privs(const std::string &cmd, bp::environment &env, file_t &file, std::error_code &ec) {
  if(!file) {
    return bp::system(cmd, env, bp::std_out > bp::null, bp::std_err > bp::null, ec);
  }

  return bp::system(cmd, env, bp::std_out > file.get(), bp::std_err > file.get(), ec);
}

boost::filesystem::path find_working_directory(const std::string &cmd, bp::environment &env) {
  // Parse the raw command string into parts to get the actual command portion
#ifdef _WIN32
  auto parts = boost::program_options::split_winmain(cmd);
#else
  auto parts = boost::program_options::split_unix(cmd);
#endif
  if(parts.empty()) {
    BOOST_LOG(error) << "Unable to parse command: "sv << cmd;
    return {};
  }

  BOOST_LOG(debug) << "Parsed executable ["sv << parts.at(0) << "] from command ["sv << cmd << ']';

  // If the cmd path is not a complete path, resolve it using our PATH variable
  boost::filesystem::path cmd_path(parts.at(0));
  if(!cmd_path.is_absolute()) {
    cmd_path = boost::process::search_path(parts.at(0));
    if(cmd_path.empty()) {
      BOOST_LOG(error) << "Unable to find executable ["sv << parts.at(0) << "]. Is it in your PATH?"sv;
      return {};
    }
  }

  BOOST_LOG(debug) << "Resolved executable ["sv << parts.at(0) << "] to path ["sv << cmd_path << ']';

  // Now that we have a complete path, we can just use parent_path()
  return cmd_path.parent_path();
}

int proc_t::execute(int app_id) {
  // Ensure starting from a clean slate
  terminate();

  bool found = false;
  ctx_t process {};
  for(auto &_app : _apps) {
    if(_app.id == app_id) {
      process = _app;
      found   = true;
      break;
    }
  }

  if(!found) {
    BOOST_LOG(error) << "Couldn't find app with ID ["sv << app_id << ']';

    return 404;
  }
  _app_id = app_id;


  _undo_begin = std::begin(process.prep_cmds);
  _undo_it    = _undo_begin;

  if(!process.output.empty() && process.output != "null"sv) {
#ifdef _WIN32
    // fopen() interprets the filename as an ANSI string on Windows, so we must convert it
    // to UTF-16 and use the wchar_t variants for proper Unicode log file path support.
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
    auto woutput = converter.from_bytes(process.output);

    // Use _SH_DENYNO to allow us to open this log file again for writing even if it is
    // still open from a previous execution. This is required to handle the case of a
    // detached process executing again while the previous process is still running.
    _pipe.reset(_wfsopen(woutput.c_str(), L"a", _SH_DENYNO));
#else
    _pipe.reset(fopen(process.output.c_str(), "a"));
#endif
  }

  std::error_code ec;
  // Executed when returning from function
  auto fg = util::fail_guard([&]() {
    terminate();
  });

  for(; _undo_it != std::end(process.prep_cmds); ++_undo_it) {
    auto &cmd = _undo_it->do_cmd;

    BOOST_LOG(info) << "Executing: ["sv << cmd << ']';
    auto ret = exe_with_full_privs(cmd, _env, _pipe, ec);

    if(ec) {
      BOOST_LOG(error) << "Couldn't run ["sv << cmd << "]: System: "sv << ec.message();
      return -1;
    }

    if(ret != 0) {
      BOOST_LOG(error) << '[' << cmd << "] failed with code ["sv << ret << ']';
      return -1;
    }
  }

  for(auto &cmd : process.detached) {
    boost::filesystem::path working_dir = process.working_dir.empty() ?
                                            find_working_directory(cmd, _env) :
                                            boost::filesystem::path(process.working_dir);
    BOOST_LOG(info) << "Spawning ["sv << cmd << "] in ["sv << working_dir << ']';
    auto child = platf::run_unprivileged(cmd, working_dir, _env, _pipe.get(), ec);
    if(ec) {
      BOOST_LOG(warning) << "Couldn't spawn ["sv << cmd << "]: System: "sv << ec.message();
    }
    else {
      child.detach();
    }
  }

  BOOST_LOG(info) << process.cmd << " " << process.cmd.size() << "\n";
  if(process.cmd.empty()) {
    BOOST_LOG(debug) << "Executing [Desktop]"sv;
    placebo = true;
  }
  else {
    boost::filesystem::path working_dir = process.working_dir.empty() ?
                                            find_working_directory(process.cmd, _env) :
                                            boost::filesystem::path(process.working_dir);
    BOOST_LOG(info) << "Executing: ["sv << process.cmd << "] in ["sv << working_dir << ']';
    _process = platf::run_unprivileged(process.cmd, working_dir, _env, _pipe.get(), ec);
    if(ec) {
      BOOST_LOG(warning) << "Couldn't run ["sv << process.cmd << "]: System: "sv << ec.message();
      return -1;
    }

    _process_handle.add(_process);
  }

  fg.disable();

  return 0;
}

int proc_t::running() {
  if(placebo || _process.running()) {
    return _app_id;
  }

  // Perform cleanup actions now if needed
  if(_process) {
    terminate();
  }

  return -1;
}

void proc_t::terminate() {
  std::error_code ec;

  // Ensure child process is terminated
  placebo = false;
  process_end(_process, _process_handle);
  _process        = bp::child();
  _process_handle = bp::group();
  _app_id         = -1;

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

    auto ret = exe_with_full_privs(cmd, _env, _pipe, ec);

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

void proc_t::add_app(int id, ctx_t app) {
  bool newApp = true;
  for(auto &_app : _apps) {
    if(_app.id == id) {
      _app   = app;
      newApp = false;
      break;
    }
  }

  if(newApp) {
    _apps.push_back(app);
  }
}

bool proc_t::remove_app(int id) {

  for(int i = 0; i < _apps.size(); i++) {
    if(_apps[i].id == id) {
      _apps.erase(_apps.begin() + i);
      return true;
    }
  }
  return false;
}

// Gets application image from application list.
// Returns image from assets directory if found there.
// Returns default image if image configuration is not set.
// Returns http content-type header compatible image type.
std::string proc_t::get_app_image(int app_id) {

  auto default_image = SUNSHINE_ASSETS_DIR "/box.png";

  std::string app_image_path;
  for(int i = 0; i < _apps.size(); i++) {
    if(_apps[i].id == app_id) {
      if(!_apps[i].image_path.empty()) {
        app_image_path = _apps[i].image_path;
      }
      break;
    }
  }
  if(app_image_path.empty()) {
    // image is empty, return default box image
    return default_image;
  }

  // get the image extension and convert it to lowercase
  auto image_extension = std::filesystem::path(app_image_path).extension().string();
  boost::to_lower(image_extension);

  // return the default box image if extension is not "png"
  if(image_extension != ".png") {
    return default_image;
  }

  // check if image is in assets directory
  auto full_image_path = std::filesystem::path(SUNSHINE_ASSETS_DIR) / app_image_path;
  if(std::filesystem::exists(full_image_path)) {
    return full_image_path.string();
  }
  else if(app_image_path == "./assets/steam.png") {
    // handle old default steam image definition
    return SUNSHINE_ASSETS_DIR "/steam.png";
  }

  // check if specified image exists
  std::error_code code;
  if(!std::filesystem::exists(app_image_path, code)) {
    // return default box image if image does not exist
    return default_image;
  }

  // image is a png, and not in assets directory
  // return only "content-type" http header compatible image type
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
        auto var_name  = std::string { var_begin, var_end };

#ifdef _WIN32
        // Windows treats environment variable names in a case-insensitive manner,
        // so we look for a case-insensitive match here. This is critical for
        // correctly appending to PATH on Windows.
        auto itr = std::find_if(env.cbegin(), env.cend(),
          [&](const auto &e) { return boost::iequals(e.get_name(), var_name); });
        if(itr != env.cend()) {
          // Use an existing case-insensitive match
          var_name = itr->get_name();
        }
#endif

        ss << env[var_name].to_string();

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

bool save(const std::string &fileName) {
  try {
    auto currentApps = json::parse(read_file(fileName.c_str()));
    json::array apps;
    for(auto &app : proc.get_apps()) {
      json::value appJson;
      apps.push_back(json::value_from(app));
    }
    currentApps.at("apps") = apps;
    write_file(fileName.c_str(), json::serialize(currentApps));
    return true;
  }
  catch(std::exception &e) {
    BOOST_LOG(error) << "Failed to save apps.json; file is corrupted.";
    return false;
  }
}

void parse(const std::string &fileName) {

  try {
    auto tree = json::parse(read_file(fileName.c_str()));

    auto &apps_node = tree.at("apps"s).as_array();
    auto &env_vars  = tree.at("env"s).as_object();

    auto this_env = boost::this_process::environment();

    for(auto &[name, val] : env_vars) {
      this_env[name] = parse_env_val(this_env, val.as_string());
    }

    std::vector<proc::ctx_t> apps;
    for(auto &app_node : apps_node) {
      apps.emplace_back(json::value_to<proc::ctx_t>(app_node));
    }

    proc = proc::proc_t { this_env, std::move(apps) };
  }
  catch(std::exception &e) {
    BOOST_LOG(error) << e.what();
  }
}


} // namespace proc
