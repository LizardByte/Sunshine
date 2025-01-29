/**
 * @file src/process.cpp
 * @brief Definitions for the startup and shutdown of the apps started by a streaming Session.
 */
#define BOOST_BIND_GLOBAL_PLACEHOLDERS

// standard includes
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

// lib includes
#include <boost/algorithm/string.hpp>
#include <boost/crc.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <openssl/evp.h>
#include <openssl/sha.h>

// local includes
#include "config.h"
#include "crypto.h"
#include "display_device.h"
#include "logging.h"
#include "platform/common.h"
#include "process.h"
#include "system_tray.h"
#include "utility.h"

#ifdef _WIN32
  // from_utf8() string conversion function
  #include "platform/windows/misc.h"

  // _SH constants for _wfsopen()
  #include <share.h>
#endif

#define DEFAULT_APP_IMAGE_PATH SUNSHINE_ASSETS_DIR "/box.png"

namespace proc {
  namespace {
    std::string_view::iterator find_match(std::string_view::iterator begin, std::string_view::iterator end) {
      int stack = 0;

      --begin;
      do {
        ++begin;
        switch (*begin) {
          case '(':
            ++stack;
            break;
          case ')':
            --stack;
        }
      } while (begin != end && stack != 0);

      if (begin == end) {
        throw std::out_of_range("Missing closing bracket \')\'");
      }
      return begin;
    }
  }  // namespace

  // The global :/
  proc_t proc;

  std::variant<std::unique_ptr<app_t>, int> app_t::start(ctx_t app_context, int app_id, boost::process::v1::environment env, const rtsp_stream::launch_session_t &launch_session) {
    // This is a simple trick for using "std::make_unique" with private constructor
    class app_constructor_t: public app_t {
      using app_t::app_t;
    };

    auto app {std::make_unique<app_constructor_t>()};
    if (int return_code {app->execute(std::move(app_context), app_id, std::move(env), launch_session)}; return_code != 0) {
      return return_code;
    }

    return app;
  }

  int app_t::execute(ctx_t app_context, int app_id, boost::process::v1::environment env, const rtsp_stream::launch_session_t &launch_session) {
    _app_id = app_id;
    _env = std::move(env);
    _context = std::move(app_context);
    _prep_cmd_it = std::begin(_context.prep_cmds);

    // Add Stream-specific environment variables
    _env["SUNSHINE_APP_ID"] = std::to_string(_app_id);
    _env["SUNSHINE_APP_NAME"] = _context.name;
    _env["SUNSHINE_CLIENT_WIDTH"] = std::to_string(launch_session.width);
    _env["SUNSHINE_CLIENT_HEIGHT"] = std::to_string(launch_session.height);
    _env["SUNSHINE_CLIENT_FPS"] = std::to_string(launch_session.fps);
    _env["SUNSHINE_CLIENT_HDR"] = launch_session.enable_hdr ? "true" : "false";
    _env["SUNSHINE_CLIENT_GCMAP"] = std::to_string(launch_session.gcmap);
    _env["SUNSHINE_CLIENT_HOST_AUDIO"] = launch_session.host_audio ? "true" : "false";
    _env["SUNSHINE_CLIENT_ENABLE_SOPS"] = launch_session.enable_sops ? "true" : "false";
    int channelCount = launch_session.surround_info & (65535);
    switch (channelCount) {
      case 2:
        _env["SUNSHINE_CLIENT_AUDIO_CONFIGURATION"] = "2.0";
        break;
      case 6:
        _env["SUNSHINE_CLIENT_AUDIO_CONFIGURATION"] = "5.1";
        break;
      case 8:
        _env["SUNSHINE_CLIENT_AUDIO_CONFIGURATION"] = "7.1";
        break;
    }
    _env["SUNSHINE_CLIENT_AUDIO_SURROUND_PARAMS"] = launch_session.surround_params;

    if (!_context.output.empty() && _context.output != "null"sv) {
#ifdef _WIN32
      // fopen() interprets the filename as an ANSI string on Windows, so we must convert it
      // to UTF-16 and use the wchar_t variants for proper Unicode log file path support.
      auto woutput = platf::from_utf8(_context.output);

      // Use _SH_DENYNO to allow us to open this log file again for writing even if it is
      // still open from a previous execution. This is required to handle the case of a
      // detached process executing again while the previous process is still running.
      _output_pipe.reset(_wfsopen(woutput.c_str(), L"a", _SH_DENYNO));
#else
      _output_pipe.reset(fopen(_context.output.c_str(), "a"));
#endif
    }

    // Executed when returning from function
    auto fg = util::fail_guard([&]() {
      terminate();
    });

    // Execute the "do" commands
    for (; _prep_cmd_it != std::end(_context.prep_cmds); ++_prep_cmd_it) {
      auto &cmd = *_prep_cmd_it;

      // Skip empty commands
      if (cmd.do_cmd.empty()) {
        continue;
      }

      boost::filesystem::path working_dir = _context.working_dir.empty() ?
                                              find_working_directory(cmd.do_cmd) :
                                              boost::filesystem::path(_context.working_dir);
      BOOST_LOG(info) << "Executing Do Cmd: ["sv << cmd.do_cmd << ']';

      std::error_code ec;
      auto child = platf::run_command(cmd.elevated, true, cmd.do_cmd, working_dir, _env, _output_pipe.get(), ec, nullptr);

      if (ec) {
        BOOST_LOG(error) << "Couldn't run ["sv << cmd.do_cmd << "]: System: "sv << ec.message();
        // We don't want any prep commands failing launch of the desktop.
        // This is to prevent the issue where users reboot their PC and need to log in with Sunshine.
        // permission_denied is typically returned when the user impersonation fails, which can happen when user is not signed in yet.
        if (!(_context.cmd.empty() && ec == std::errc::permission_denied)) {
          return -1;
        }
      }

      child.wait();
      auto ret = child.exit_code();
      if (ret != 0 && ec != std::errc::permission_denied) {
        BOOST_LOG(error) << '[' << cmd.do_cmd << "] failed with code ["sv << ret << ']';
        return -1;
      }
    }

    // Execute the "detached" commands
    for (auto &cmd : _context.detached) {
      boost::filesystem::path working_dir = _context.working_dir.empty() ?
                                              find_working_directory(cmd) :
                                              boost::filesystem::path(_context.working_dir);
      BOOST_LOG(info) << "Spawning ["sv << cmd << "] in ["sv << working_dir << ']';

      std::error_code ec;
      auto child = platf::run_command(_context.elevated, true, cmd, working_dir, _env, _output_pipe.get(), ec, nullptr);

      if (ec) {
        BOOST_LOG(warning) << "Couldn't spawn ["sv << cmd << "]: System: "sv << ec.message();
      } else {
        child.detach();
      }
    }

    // Execute the main command if available
    if (_context.cmd.empty()) {
      BOOST_LOG(info) << "Executing [Desktop]"sv;
      _is_detached = true;
    } else {
      boost::filesystem::path working_dir = _context.working_dir.empty() ?
                                              find_working_directory(_context.cmd) :
                                              boost::filesystem::path(_context.working_dir);
      BOOST_LOG(info) << "Executing: ["sv << _context.cmd << "] in ["sv << working_dir << ']';

      std::error_code ec;
      _process = platf::run_command(_context.elevated, true, _context.cmd, working_dir, _env, _output_pipe.get(), ec, &_process_group);

      if (ec) {
        BOOST_LOG(warning) << "Couldn't run ["sv << _context.cmd << "]: System: "sv << ec.message();
        return -1;
      }
    }

    _launch_time = std::chrono::steady_clock::now();
    fg.disable();
    return 0;
  }

  app_t::~app_t() {
    try {
      terminate();
    } catch (const std::exception &e) {
      BOOST_LOG(fatal) << "Exception in `app_t` destructor: " << e.what();
    }
  }

  std::optional<int> app_t::get_app_id() {
#ifndef _WIN32
    // On POSIX OSes, we must periodically wait for our children to avoid
    // them becoming zombies. This must be synchronized carefully with
    // calls to bp::wait() and platf::process_group_running() which both
    // invoke waitpid() under the hood.
    auto reaper = util::fail_guard([]() {
      while (waitpid(-1, nullptr, WNOHANG) > 0);
    });
#endif

    if (_is_detached) {
      return _app_id;
    } else if (_context.wait_all && _process_group && platf::process_group_running((std::uintptr_t) _process_group.native_handle())) {
      // The app is still running if any process in the group is still running
      return _app_id;
    } else if (_process.running()) {
      // The app is still running only if the initial process launched is still running
      return _app_id;
    } else if (_context.auto_detach && _process.native_exit_code() == 0 &&
               std::chrono::steady_clock::now() - _launch_time < 5s) {
      BOOST_LOG(info) << "App exited gracefully within 5 seconds of launch. Treating the app as a detached command."sv;
      BOOST_LOG(info) << "Adjust this behavior in the Applications tab or apps.json if this is not what you want."sv;
      _is_detached = true;
      return _app_id;
    }

    // Perform cleanup actions now if needed
    if (_process) {
      BOOST_LOG(info) << "App exited with code ["sv << _process.native_exit_code() << ']';
      terminate();
    }

    return std::nullopt;
  }

  void app_t::terminate() {
    // Perform cleanup for the actual process first
    terminate_process_group(_process, _process_group, _context.exit_timeout);
    _process = {};
    _process_group = {};

    // Execute undo commands
    for (; _prep_cmd_it != std::begin(_context.prep_cmds); --_prep_cmd_it) {
      const auto &cmd = *std::prev(_prep_cmd_it);

      if (cmd.undo_cmd.empty()) {
        continue;
      }

      boost::filesystem::path working_dir = _context.working_dir.empty() ?
                                              find_working_directory(cmd.undo_cmd) :
                                              boost::filesystem::path(_context.working_dir);
      BOOST_LOG(info) << "Executing Undo Cmd: ["sv << cmd.undo_cmd << ']';

      std::error_code ec;
      auto child = platf::run_command(cmd.elevated, true, cmd.undo_cmd, working_dir, _env, _output_pipe.get(), ec, nullptr);

      if (ec) {
        BOOST_LOG(warning) << "System: "sv << ec.message();
      }

      child.wait();
      auto ret = child.exit_code();

      if (ret != 0) {
        BOOST_LOG(warning) << "Return code ["sv << ret << ']';
      }
    }

    // Close the output pipe
    _output_pipe.reset();
  }

  void app_t::terminate_process_group(boost::process::v1::child &proc, boost::process::v1::group &group, std::chrono::seconds exit_timeout) {
    if (group.valid() && platf::process_group_running((std::uintptr_t) group.native_handle())) {
      if (exit_timeout.count() > 0) {
        // Request processes in the group to exit gracefully
        if (platf::request_process_group_exit((std::uintptr_t) group.native_handle())) {
          // If the request was successful, wait for a little while for them to exit.
          BOOST_LOG(info) << "Successfully requested the app to exit. Waiting up to "sv << exit_timeout.count() << " seconds for it to close."sv;

          // group::wait_for() and similar functions are broken and deprecated, so we use a simple polling loop
          while (platf::process_group_running((std::uintptr_t) group.native_handle()) && (--exit_timeout).count() >= 0) {
            std::this_thread::sleep_for(1s);
          }

          if (exit_timeout.count() < 0) {
            BOOST_LOG(warning) << "App did not fully exit within the timeout. Terminating the app's remaining processes."sv;
          } else {
            BOOST_LOG(info) << "All app processes have successfully exited."sv;
          }
        } else {
          BOOST_LOG(info) << "App did not respond to a graceful termination request. Forcefully terminating the app's processes."sv;
        }
      } else {
        BOOST_LOG(info) << "No graceful exit timeout was specified for this app. Forcefully terminating the app's processes."sv;
      }

      // We always call terminate() even if we waited successfully for all processes above.
      // This ensures the process group state is consistent with the OS in boost.
      std::error_code ec;
      group.terminate(ec);
      group.detach();
    }

    if (proc.valid()) {
      // avoid zombie process
      proc.detach();
    }
  }

  proc_t::~proc_t() {
    // It's not safe to call terminate() here because our proc_t is a static variable
    // that may be destroyed after the Boost loggers have been destroyed. Instead,
    // we return a deinit_t to main() to handle termination when we're exiting.
    // Once we reach this point here, termination must have already happened.
    assert(!_started_app);
  }

  int proc_t::execute(int app_id, std::shared_ptr<rtsp_stream::launch_session_t> launch_session) {
    std::lock_guard lock {_mutex};

    // Ensure starting from a clean slate
    terminate();
    if (!launch_session) {
      BOOST_LOG(error) << "nullptr given to proc_t::execute!"sv;
      return -1;
    }

    auto iter = std::find_if(_apps.begin(), _apps.end(), [&app_id](const auto &app) {
      return app.id == std::to_string(app_id);
    });

    if (iter == _apps.end()) {
      BOOST_LOG(error) << "Couldn't find app with ID ["sv << app_id << ']';
      return 404;
    }

    // Keep the last run app name regardless of whether we manage to start it or not.
    _last_run_app_name = iter->name;

    auto app_or_exit_code {app_t::start(*iter, app_id, _env, *launch_session)};
    if (auto *app = std::get_if<0>(&app_or_exit_code); app) {
      // Save the started app and return exit code 0.
      _started_app = std::move(*app);
      return 0;
    }

    // Exit code of the failed start
    return std::get<1>(app_or_exit_code);
  }

  std::optional<int> proc_t::get_running_app_id() {
    std::lock_guard lock {_mutex};

    if (_started_app) {
      return _started_app->get_app_id();
    }

    return std::nullopt;
  }

  void proc_t::terminate() {
    std::lock_guard lock {_mutex};

    if (_started_app) {
#if defined SUNSHINE_TRAY && SUNSHINE_TRAY >= 1
      // Only show the Stopped notification if we actually have an app to stop
      // Since terminate() is always run when a new app has started
      if (!get_last_run_app_name().empty()) {
        system_tray::update_tray_stopped(get_last_run_app_name());
      }
#endif

      display_device::revert_configuration();
    }

    _started_app = nullptr;
  }

  std::vector<ctx_t> proc_t::get_apps() const {
    std::lock_guard lock {_mutex};
    return _apps;
  }

  std::string proc_t::get_app_image(int app_id) const {
    std::lock_guard lock {_mutex};

    auto iter = std::find_if(_apps.begin(), _apps.end(), [&app_id](const auto &app) {
      return app.id == std::to_string(app_id);
    });
    auto app_image_path = iter == _apps.end() ? std::string() : iter->image_path;

    return validate_app_image_path(app_image_path);
  }

  std::string proc_t::get_last_run_app_name() const {
    std::lock_guard lock {_mutex};
    return _last_run_app_name;
  }

  void proc_t::refresh(const std::string &file_name) {
    std::lock_guard lock {_mutex};

    auto opt_env_and_apps {parse(file_name)};
    if (opt_env_and_apps) {
      auto [env, apps] = *opt_env_and_apps;

      _env = std::move(env);
      _apps = std::move(apps);
    }
  }

  std::unique_ptr<platf::deinit_t> init() {
    class deinit_t: public platf::deinit_t {
    public:
      ~deinit_t() override {
        proc.terminate();
      }
    };

    return std::make_unique<deinit_t>();
  }

  boost::filesystem::path find_working_directory(const std::string &cmd) {
    // Parse the raw command string into parts to get the actual command portion
#ifdef _WIN32
    auto parts = boost::program_options::split_winmain(cmd);
#else
    auto parts = boost::program_options::split_unix(cmd);
#endif
    if (parts.empty()) {
      BOOST_LOG(error) << "Unable to parse command: "sv << cmd;
      return {};
    }

    BOOST_LOG(debug) << "Parsed target ["sv << parts.at(0) << "] from command ["sv << cmd << ']';

    // If the target is a URL, don't parse any further here
    if (parts.at(0).find("://") != std::string::npos) {
      return {};
    }

    // If the cmd path is not an absolute path, resolve it using our PATH variable
    boost::filesystem::path cmd_path(parts.at(0));
    if (!cmd_path.is_absolute()) {
      cmd_path = boost::process::v1::search_path(parts.at(0));
      if (cmd_path.empty()) {
        BOOST_LOG(error) << "Unable to find executable ["sv << parts.at(0) << "]. Is it in your PATH?"sv;
        return {};
      }
    }

    BOOST_LOG(debug) << "Resolved target ["sv << parts.at(0) << "] to path ["sv << cmd_path << ']';

    // Now that we have a complete path, we can just use parent_path()
    return cmd_path.parent_path();
  }

  std::string parse_env_val(const boost::process::v1::native_environment &env, std::string_view val_raw) {
    auto pos = std::begin(val_raw);
    auto dollar = std::find(pos, std::end(val_raw), '$');

    std::stringstream ss;

    while (dollar != std::end(val_raw)) {
      auto next = dollar + 1;
      if (next != std::end(val_raw)) {
        switch (*next) {
          case '(':
            {
              ss.write(pos, (dollar - pos));
              auto var_begin = next + 1;
              auto var_end = find_match(next, std::end(val_raw));
              auto var_name = std::string {var_begin, var_end};

#ifdef _WIN32
              {
                // Windows treats environment variable names in a case-insensitive manner,
                // so we look for a case-insensitive match here. This is critical for
                // correctly appending to PATH on Windows.
                auto itr = std::find_if(env.cbegin(), env.cend(), [&](const auto &e) {
                  return boost::iequals(e.get_name(), var_name);
                });
                if (itr != env.cend()) {
                  // Use an existing case-insensitive match
                  var_name = itr->get_name();
                }
              }
#endif

              if (auto env_it = env.find(var_name); env_it != env.end()) {
                ss << env_it->to_string();
              }

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
      } else {
        dollar = next;
      }
    }

    ss.write(pos, (dollar - pos));

    return ss.str();
  }

  std::string validate_app_image_path(const std::string &app_image_path) {
    if (app_image_path.empty()) {
      return DEFAULT_APP_IMAGE_PATH;
    }

    // get the image extension and convert it to lowercase
    auto image_extension = std::filesystem::path(app_image_path).extension().string();
    boost::to_lower(image_extension);

    // return the default box image if extension is not "png"
    if (image_extension != ".png") {
      return DEFAULT_APP_IMAGE_PATH;
    }

    // check if image is in assets directory
    auto full_image_path = std::filesystem::path(SUNSHINE_ASSETS_DIR) / app_image_path;
    if (std::filesystem::exists(full_image_path)) {
      return full_image_path.string();
    } else if (app_image_path == "./assets/steam.png") {
      // handle old default steam image definition
      return SUNSHINE_ASSETS_DIR "/steam.png";
    }

    // check if specified image exists
    std::error_code code;
    if (!std::filesystem::exists(app_image_path, code)) {
      // return default box image if image does not exist
      BOOST_LOG(warning) << "Couldn't find app image at path ["sv << app_image_path << ']';
      return DEFAULT_APP_IMAGE_PATH;
    }

    // image is a png, and not in assets directory
    // return only "content-type" http header compatible image type
    return app_image_path;
  }

  std::optional<std::string> calculate_sha256(const std::string &file_name) {
    crypto::md_ctx_t ctx {EVP_MD_CTX_create()};
    if (!ctx) {
      return std::nullopt;
    }

    if (!EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr)) {
      return std::nullopt;
    }

    // Read file and update calculated SHA
    char buf[1024 * 16];
    std::ifstream file(file_name, std::ifstream::binary);
    while (file.good()) {
      file.read(buf, sizeof(buf));
      if (!EVP_DigestUpdate(ctx.get(), buf, file.gcount())) {
        return std::nullopt;
      }
    }
    file.close();

    unsigned char result[SHA256_DIGEST_LENGTH];
    if (!EVP_DigestFinal_ex(ctx.get(), result, nullptr)) {
      return std::nullopt;
    }

    // Transform byte-array to string
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (const auto &byte : result) {
      ss << std::setw(2) << (int) byte;
    }
    return ss.str();
  }

  uint32_t calculate_crc32(const std::string &input) {
    boost::crc_32_type result;
    result.process_bytes(input.data(), input.length());
    return result.checksum();
  }

  std::tuple<std::string, std::string> calculate_app_id(const std::string &app_name, const std::string &app_image_path, int app_index) {
    // Generate id by hashing name with image data if present
    std::vector<std::string> to_hash;
    to_hash.push_back(app_name);
    auto file_path = validate_app_image_path(app_image_path);
    if (file_path != DEFAULT_APP_IMAGE_PATH) {
      auto file_hash = calculate_sha256(file_path);
      if (file_hash) {
        to_hash.push_back(file_hash.value());
      } else {
        // Fallback to just hashing image path
        to_hash.push_back(file_path);
      }
    }

    // Create combined strings for hash
    std::stringstream ss;
    for_each(to_hash.begin(), to_hash.end(), [&ss](const std::string &s) {
      ss << s;
    });
    auto input_no_index = ss.str();
    ss << app_index;
    auto input_with_index = ss.str();

    // CRC32 then truncate to signed 32-bit range due to client limitations
    auto id_no_index = std::to_string(abs((int32_t) calculate_crc32(input_no_index)));
    auto id_with_index = std::to_string(abs((int32_t) calculate_crc32(input_with_index)));

    return std::make_tuple(id_no_index, id_with_index);
  }

  std::optional<std::tuple<boost::process::v1::environment, std::vector<ctx_t>>> parse(const std::string &file_name) {
    namespace pt = boost::property_tree;

    try {
      pt::ptree tree;
      pt::read_json(file_name, tree);

      auto &apps_node = tree.get_child("apps"s);
      auto &env_vars = tree.get_child("env"s);

      auto this_env = boost::this_process::environment();

      for (auto &[name, val] : env_vars) {
        this_env[name] = parse_env_val(this_env, val.get_value<std::string>());
      }

      std::set<std::string> ids;
      std::vector<ctx_t> apps;
      int i = 0;
      for (auto &[_, app_node] : apps_node) {
        ctx_t ctx;

        auto prep_nodes_opt = app_node.get_child_optional("prep-cmd"s);
        auto detached_nodes_opt = app_node.get_child_optional("detached"s);
        auto exclude_global_prep = app_node.get_optional<bool>("exclude-global-prep-cmd"s);
        auto output = app_node.get_optional<std::string>("output"s);
        auto name = parse_env_val(this_env, app_node.get<std::string>("name"s));
        auto cmd = app_node.get_optional<std::string>("cmd"s);
        auto image_path = app_node.get_optional<std::string>("image-path"s);
        auto working_dir = app_node.get_optional<std::string>("working-dir"s);
        auto elevated = app_node.get_optional<bool>("elevated"s);
        auto auto_detach = app_node.get_optional<bool>("auto-detach"s);
        auto wait_all = app_node.get_optional<bool>("wait-all"s);
        auto exit_timeout = app_node.get_optional<int>("exit-timeout"s);

        std::vector<cmd_t> prep_cmds;
        if (!exclude_global_prep.value_or(false)) {
          prep_cmds.reserve(config::sunshine.prep_cmds.size());
          for (auto &prep_cmd : config::sunshine.prep_cmds) {
            auto do_cmd = parse_env_val(this_env, prep_cmd.do_cmd);
            auto undo_cmd = parse_env_val(this_env, prep_cmd.undo_cmd);

            prep_cmds.emplace_back(
              std::move(do_cmd),
              std::move(undo_cmd),
              std::move(prep_cmd.elevated)
            );
          }
        }

        if (prep_nodes_opt) {
          auto &prep_nodes = *prep_nodes_opt;

          prep_cmds.reserve(prep_cmds.size() + prep_nodes.size());
          for (auto &[_, prep_node] : prep_nodes) {
            auto do_cmd = prep_node.get_optional<std::string>("do"s);
            auto undo_cmd = prep_node.get_optional<std::string>("undo"s);
            auto elevated = prep_node.get_optional<bool>("elevated");

            prep_cmds.emplace_back(
              parse_env_val(this_env, do_cmd.value_or("")),
              parse_env_val(this_env, undo_cmd.value_or("")),
              std::move(elevated.value_or(false))
            );
          }
        }

        std::vector<std::string> detached;
        if (detached_nodes_opt) {
          auto &detached_nodes = *detached_nodes_opt;

          detached.reserve(detached_nodes.size());
          for (auto &[_, detached_val] : detached_nodes) {
            detached.emplace_back(parse_env_val(this_env, detached_val.get_value<std::string>()));
          }
        }

        if (output) {
          ctx.output = parse_env_val(this_env, *output);
        }

        if (cmd) {
          ctx.cmd = parse_env_val(this_env, *cmd);
        }

        if (working_dir) {
          ctx.working_dir = parse_env_val(this_env, *working_dir);
#ifdef _WIN32
          // The working directory, unlike the command itself, should not be quoted
          // when it contains spaces. Unlike POSIX, Windows forbids quotes in paths,
          // so we can safely strip them all out here to avoid confusing the user.
          boost::erase_all(ctx.working_dir, "\"");
#endif
        }

        if (image_path) {
          ctx.image_path = parse_env_val(this_env, *image_path);
        }

        ctx.elevated = elevated.value_or(false);
        ctx.auto_detach = auto_detach.value_or(true);
        ctx.wait_all = wait_all.value_or(true);
        ctx.exit_timeout = std::chrono::seconds {exit_timeout.value_or(5)};

        auto possible_ids = calculate_app_id(name, ctx.image_path, i++);
        if (ids.count(std::get<0>(possible_ids)) == 0) {
          // Avoid using index to generate id if possible
          ctx.id = std::get<0>(possible_ids);
        } else {
          // Fallback to include index on collision
          ctx.id = std::get<1>(possible_ids);
        }
        ids.insert(ctx.id);

        ctx.name = std::move(name);
        ctx.prep_cmds = std::move(prep_cmds);
        ctx.detached = std::move(detached);

        apps.emplace_back(std::move(ctx));
      }

      return std::make_tuple(std::move(this_env), std::move(apps));
    } catch (std::exception &e) {
      BOOST_LOG(error) << e.what();
    }

    return std::nullopt;
  }
}  // namespace proc
