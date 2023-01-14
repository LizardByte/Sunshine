// Created by loki on 12/14/19.

#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include "process.h"

#include <filesystem>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/crc.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <openssl/evp.h>
#include <openssl/sha.h>

#include "crypto.h"
#include "main.h"
#include "platform/common.h"
#include "utility.h"

#ifdef _WIN32
// _SH constants for _wfsopen()
#include <share.h>
#endif

#define DEFAULT_APP_IMAGE_PATH SUNSHINE_ASSETS_DIR "/box.png"

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
    return boost::filesystem::path();
  }

  BOOST_LOG(debug) << "Parsed executable ["sv << parts.at(0) << "] from command ["sv << cmd << ']';

  // If the cmd path is not a complete path, resolve it using our PATH variable
  boost::filesystem::path cmd_path(parts.at(0));
  if(!cmd_path.is_complete()) {
    cmd_path = boost::process::search_path(parts.at(0));
    if(cmd_path.empty()) {
      BOOST_LOG(error) << "Unable to find executable ["sv << parts.at(0) << "]. Is it in your PATH?"sv;
      return boost::filesystem::path();
    }
  }

  BOOST_LOG(debug) << "Resolved executable ["sv << parts.at(0) << "] to path ["sv << cmd_path << ']';

  // Now that we have a complete path, we can just use parent_path()
  return cmd_path.parent_path();
}

int proc_t::execute(int app_id) {
  // Ensure starting from a clean slate
  terminate();

  auto iter = std::find_if(_apps.begin(), _apps.end(), [&app_id](const auto app) {
    return app.id == std::to_string(app_id);
  });

  if(iter == _apps.end()) {
    BOOST_LOG(error) << "Couldn't find app with ID ["sv << app_id << ']';
    return 404;
  }

  _app_id    = app_id;
  auto &proc = *iter;

  _undo_begin = std::begin(proc.prep_cmds);
  _undo_it    = _undo_begin;

  if(!proc.output.empty() && proc.output != "null"sv) {
#ifdef _WIN32
    // fopen() interprets the filename as an ANSI string on Windows, so we must convert it
    // to UTF-16 and use the wchar_t variants for proper Unicode log file path support.
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
    auto woutput = converter.from_bytes(proc.output);

    // Use _SH_DENYNO to allow us to open this log file again for writing even if it is
    // still open from a previous execution. This is required to handle the case of a
    // detached process executing again while the previous process is still running.
    _pipe.reset(_wfsopen(woutput.c_str(), L"a", _SH_DENYNO));
#else
    _pipe.reset(fopen(proc.output.c_str(), "a"));
#endif
  }

  std::error_code ec;
  // Executed when returning from function
  auto fg = util::fail_guard([&]() {
    terminate();
  });

  for(; _undo_it != std::end(proc.prep_cmds); ++_undo_it) {
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

  for(auto &cmd : proc.detached) {
    boost::filesystem::path working_dir = proc.working_dir.empty() ?
                                            find_working_directory(cmd, _env) :
                                            boost::filesystem::path(proc.working_dir);
    BOOST_LOG(info) << "Spawning ["sv << cmd << "] in ["sv << working_dir << ']';
    auto child = platf::run_unprivileged(cmd, working_dir, _env, _pipe.get(), ec, nullptr);
    if(ec) {
      BOOST_LOG(warning) << "Couldn't spawn ["sv << cmd << "]: System: "sv << ec.message();
    }
    else {
      child.detach();
    }
  }

  if(proc.cmd.empty()) {
    BOOST_LOG(info) << "Executing [Desktop]"sv;
    placebo = true;
  }
  else {
    boost::filesystem::path working_dir = proc.working_dir.empty() ?
                                            find_working_directory(proc.cmd, _env) :
                                            boost::filesystem::path(proc.working_dir);
    BOOST_LOG(info) << "Executing: ["sv << proc.cmd << "] in ["sv << working_dir << ']';
    _process = platf::run_unprivileged(proc.cmd, working_dir, _env, _pipe.get(), ec, &_process_handle);
    if(ec) {
      BOOST_LOG(warning) << "Couldn't run ["sv << proc.cmd << "]: System: "sv << ec.message();
      return -1;
    }
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

  return 0;
}

void proc_t::terminate() {
  std::error_code ec;

  // Ensure child process is terminated
  placebo = false;
  process_end(_process, _process_handle);
  _process        = bp::child();
  _process_handle = bp::group();
  _app_id         = -1;

  for(; _undo_it != _undo_begin; --_undo_it) {
    auto &cmd = (_undo_it - 1)->undo_cmd;

    if(cmd.empty()) {
      continue;
    }

    BOOST_LOG(info) << "Executing: ["sv << cmd << ']';

    auto ret = exe_with_full_privs(cmd, _env, _pipe, ec);

    if(ec) {
      BOOST_LOG(warning) << "System: "sv << ec.message();
    }

    if(ret != 0) {
      BOOST_LOG(warning) << "Return code ["sv << ret << ']';
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

// Gets application image from application list.
// Returns image from assets directory if found there.
// Returns default image if image configuration is not set.
// Returns http content-type header compatible image type.
std::string proc_t::get_app_image(int app_id) {
  auto iter           = std::find_if(_apps.begin(), _apps.end(), [&app_id](const auto app) {
    return app.id == std::to_string(app_id);
  });
  auto app_image_path = iter == _apps.end() ? std::string() : iter->image_path;

  return validate_app_image_path(app_image_path);
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

std::string validate_app_image_path(std::string app_image_path) {
  if(app_image_path.empty()) {
    return DEFAULT_APP_IMAGE_PATH;
  }

  // get the image extension and convert it to lowercase
  auto image_extension = std::filesystem::path(app_image_path).extension().string();
  boost::to_lower(image_extension);

  // return the default box image if extension is not "png"
  if(image_extension != ".png") {
    return DEFAULT_APP_IMAGE_PATH;
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
    BOOST_LOG(warning) << "Couldn't find app image at path ["sv << app_image_path << ']';
    return DEFAULT_APP_IMAGE_PATH;
  }

  // image is a png, and not in assets directory
  // return only "content-type" http header compatible image type
  return app_image_path;
}

std::optional<std::string> calculate_sha256(const std::string &filename) {
  crypto::md_ctx_t ctx { EVP_MD_CTX_create() };
  if(!ctx) {
    return std::nullopt;
  }

  if(!EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr)) {
    return std::nullopt;
  }

  // Read file and update calculated SHA
  char buf[1024 * 16];
  std::ifstream file(filename, std::ifstream::binary);
  while(file.good()) {
    file.read(buf, sizeof(buf));
    if(!EVP_DigestUpdate(ctx.get(), buf, file.gcount())) {
      return std::nullopt;
    }
  }
  file.close();

  unsigned char result[SHA256_DIGEST_LENGTH];
  if(!EVP_DigestFinal_ex(ctx.get(), result, nullptr)) {
    return std::nullopt;
  }

  // Transform byte-array to string
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for(const auto &byte : result) {
    ss << std::setw(2) << (int)byte;
  }
  return ss.str();
}

uint32_t calculate_crc32(const std::string &input) {
  boost::crc_32_type result;
  result.process_bytes(input.data(), input.length());
  return result.checksum();
}

std::tuple<std::string, std::string> calculate_app_id(const std::string &app_name, std::string app_image_path, int index) {
  // Generate id by hashing name with image data if present
  std::vector<std::string> to_hash;
  to_hash.push_back(app_name);
  auto file_path = validate_app_image_path(app_image_path);
  if(file_path != DEFAULT_APP_IMAGE_PATH) {
    auto file_hash = calculate_sha256(file_path);
    if(file_hash) {
      to_hash.push_back(file_hash.value());
    }
    else {
      // Fallback to just hashing image path
      to_hash.push_back(file_path);
    }
  }

  // Create combined strings for hash
  std::stringstream ss;
  for_each(to_hash.begin(), to_hash.end(), [&ss](const std::string &s) { ss << s; });
  auto input_no_index = ss.str();
  ss << index;
  auto input_with_index = ss.str();

  // CRC32 then truncate to signed 32-bit range due to client limitations
  auto id_no_index   = std::to_string(abs((int32_t)calculate_crc32(input_no_index)));
  auto id_with_index = std::to_string(abs((int32_t)calculate_crc32(input_with_index)));

  return std::make_tuple(id_no_index, id_with_index);
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

    std::set<std::string> ids;
    std::vector<proc::ctx_t> apps;
    int i = 0;
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

      auto possible_ids = calculate_app_id(name, ctx.image_path, i++);
      if(ids.count(std::get<0>(possible_ids)) == 0) {
        // Avoid using index to generate id if possible
        ctx.id = std::get<0>(possible_ids);
      }
      else {
        // Fallback to include index on collision
        ctx.id = std::get<1>(possible_ids);
      }
      ids.insert(ctx.id);

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
    proc = std::move(*proc_opt);
  }
}
} // namespace proc
