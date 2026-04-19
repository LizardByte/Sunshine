/**
 * @file src/platform/linux/kwingrab.cpp
 * @brief KWin direct ScreenCast capture via zkde_screencast_unstable_v1 Wayland protocol.
 *
 * Bypasses xdg-desktop-portal entirely. Sunshine connects directly to KWin's
 * Wayland protocol to obtain a PipeWire node_id, then streams frames via PipeWire.
 *
 * Chain: KWin (DRM) -> Wayland zkde_screencast_v1 -> PipeWire -> Sunshine -> NVENC -> Moonlight
 */
// standard includes
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <pwd.h>
#include <string>
#include <string_view>
#include <thread>

// lib includes
#include <pipewire/pipewire.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>

// generated protocol header
#include <kde-output-order-v1.h>
#include <zkde-screencast-unstable-v1.h>

// local includes
#include "cuda.h"
#include "graphics.h"
#include "kde-output-order-v1.h"
#include "pipewire.cpp"
#include "src/platform/common.h"
#include "src/video.h"
#include "vaapi.h"

namespace {
  // KDE ScreenCast cursor modes (from protocol enum)
  constexpr uint32_t CURSOR_HIDDEN = 1;
  constexpr uint32_t CURSOR_EMBEDDED = 2;
  constexpr uint32_t CURSOR_METADATA = 4;
}  // namespace

using namespace std::literals;

extern const wl_interface wl_output_interface;

namespace kwin {
  // ─── KWin Wayland ScreenCast permissions ──────────────────────────────────────────────
  //
  // To have access to zkde_screencast_unstable_v1 KWin checks for a .desktop file with
  // X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1 and the current executable name
  // in the Exec= parameter.
  class screencast_permission_helper_t {
  public:
    static void setup() {
      if (initialized) {
        return;
      }
      if (getenvstr("KWIN_WAYLAND_NO_PERMISSION_CHECKS") == "1") {
        BOOST_LOG(info) << "[kwingrab] No permission file necessary. KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 found.";
        create_file = false;
        initialized = true;
        return;
      }
      auto filenameprefix = std::format("{}.kwin", PROJECT_FQDN);
      auto executablepath = get_executable_full_path();

      // System: Check system XDG applications for permission (usually installed with Sunshine)
      if (check_kwin_system_permissions(filenameprefix, executablepath)) {
        create_file = false;
        initialized = true;
        return;
      }

      // User: Check and (if necessary) update user's XDG applications for permission
      auto user_applications = get_xdg_user_applications_path();
      // Create non-existing application directory so we can write into it
      if (!std::filesystem::exists(user_applications) && !std::filesystem::create_directories(user_applications)) {
        // In case of failure log and return
        BOOST_LOG(error) << "[kwingrab] Failed to create application directory. Cannot continue with permission setup.";
        create_file = false;
        initialized = true;
        return;
      }
      auto user_filepathprefix = (user_applications / filenameprefix).string();
      // Generate a unique file identifier based on current unixtime
      auto user_filepathidentifier = std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
      auto user_filepath = std::format("{}{}.desktop", user_filepathprefix, user_filepathidentifier);
      for (const auto &path : std::filesystem::directory_iterator(user_applications)) {
        // List existing files for prefix and check if they contain this executable or remove them
        const auto entry = path.path().string();
        if (entry.starts_with(user_filepathprefix)) {
          auto entry_executablepath = get_executable_from_desktop_file(entry);
          if (!entry_executablepath.empty() && entry_executablepath == executablepath) {
            // This entry is exactly the one we need
            BOOST_LOG(debug) << "[kwingrab] Ignoring current temporary KWin wayland permission file: "sv << entry;
            create_file = false;
            continue;
          }
          if (!entry_executablepath.empty() && std::filesystem::exists(entry_executablepath)) {
            // This entry is for another sunshine executable that still exists
            BOOST_LOG(debug) << "[kwingrab] Ignoring other valid temporary KWin wayland permission file: "sv << entry;
            continue;
          }
          if (std::filesystem::remove(path)) {
            BOOST_LOG(info) << "[kwingrab] Removed stale temporary KWin wayland permission file: "sv << entry << " executable: "sv << entry_executablepath;
          } else {
            BOOST_LOG(warning) << "[kwingrab] Failed to remove stale temporary KWin wayland permission file: "sv << entry << " executable: "sv << entry_executablepath;
          }
        }
      }
      if (create_file) {
        // Write new file if necessary
        std::ofstream filestream(user_filepath);
        if (filestream.is_open()) {
          filestream << "[Desktop Entry]" << std::endl
                     << "Exec=" << executablepath << std::endl
                     << "X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1" << std::endl
                     << "Type=Application" << std::endl
                     << "Name="sv << PROJECT_FQDN << "-kwin-wayland-permission" << std::endl
                     << "Comment=Sunshine KWin screencast permission" << std::endl
                     << "NoDisplay=true" << std::endl;
          filestream.close();
          // Give KWin time to catch up to the new desktop file
          BOOST_LOG(info) << "[kwingrab] Created temporary KWin wayland permission file: "sv << user_filepath << " - Waiting 3 seconds for KDE to pick up new file.";
          std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        } else {
          BOOST_LOG(warning) << "[kwingrab] Failed to open temporary KWin wayland permission file: "sv << user_filepath;
        }
      }

      initialized = true;
    }

    static bool is_newly_initialized() {
      return create_file;
    }

  private:
    static inline bool initialized = false;
    static inline bool create_file = true;

    static std::string getenvstr(std::string const &key) {
      char const *val = std::getenv(key.c_str());
      if (!val) {
        return "";
      }
      return val;
    }

    static std::filesystem::path get_xdg_user_applications_path() {
      std::string homedir = getenvstr("HOME");
      if (homedir.empty()) {
        homedir = getpwuid(geteuid())->pw_dir;
      }
      // Follow the XDG base directory specification for user data home:
      // https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
      std::filesystem::path xdg_data_home;
      if (std::string dir = getenvstr("XDG_DATA_HOME"); !dir.empty()) {
        xdg_data_home = std::filesystem::path(dir);
      } else {
        xdg_data_home = std::filesystem::path(homedir) / ".local"sv / "share"sv;
      }
      return xdg_data_home / "applications";
    }

    static std::string get_executable_full_path() {
      // Adapted from https://linuxvox.com/blog/how-do-i-find-the-location-of-the-executable-in-c/
      char exe_path[PATH_MAX];  // PATH_MAX is defined in limits.h (e.g., 4096 on Linux)
      ssize_t len;
      // Read the symlink /proc/self/exe into exe_path
      len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
      if (len == -1) {
        return "";
      }
      return std::string(exe_path, len);
    }

    static std::string get_executable_from_desktop_file(const std::string &path) {
      if (std::ifstream file(path); file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
          if (line.starts_with("Exec=") && line.length() > 5) {
            return line.substr(5);
          }
        }
      }
      return "";
    }

    static bool check_kwin_system_permissions(const std::string_view &filenameprefix, const std::string_view &executablepath) {
      // Find data dirs to check from XDG_DATA_DIRS
      std::vector<std::string> xdg_data_dirs;
      if (const std::string e = getenvstr("XDG_DATA_DIRS"); !e.empty()) {
        std::stringstream ss(e);
        std::string item;

        while (getline(ss, item, ':')) {  // : is likely valid for all OSes supported, if a constant is available it should be used instead
          xdg_data_dirs.push_back(item);
        }
      }
      // Use defaults from https://specifications.freedesktop.org/basedir/latest/ if ENV var was empty
      if (xdg_data_dirs.empty()) {
        xdg_data_dirs.emplace_back("/usr/local/share/");
        xdg_data_dirs.emplace_back("/usr/share/");
      }
      // Check for ${filenameprefix}.desktop in each directory
      for (auto const &dir : xdg_data_dirs) {
        std::string filename = std::format("{0}{1}applications{1}{2}.desktop", dir, boost::filesystem::path::preferred_separator, filenameprefix);
        if (std::filesystem::exists(filename)) {
          auto file_executablepath = get_executable_from_desktop_file(filename);
          if (file_executablepath == executablepath) {
            BOOST_LOG(info) << "[kwingrab] Found matching system KWin desktop permission file: "sv << filename;
            return true;
          }
        }

      }
      return false;
    }
  };

  // ─── Wayland ScreenCast session ──────────────────────────────────────────────
  //
  // Owns its own wl_display connection. Binds zkde_screencast_unstable_v1
  // and wl_output from the registry, then calls stream_output() to start
  // a ScreenCast. Waits for the created(node_id) event from KWin.
  class screencast_t {
  public:
    screencast_t &operator=(screencast_t &&) = delete;  // Do not allow to copying

    ~screencast_t() {
      if (stream) {
        zkde_screencast_stream_unstable_v1_close(stream);
        stream = nullptr;
      }

      if (zkde_screencast) {
        zkde_screencast_unstable_v1_destroy(zkde_screencast);
        zkde_screencast = nullptr;
      }

      if (kde_output_order) {
        kde_output_order_v1_destroy(kde_output_order);
        kde_output_order = nullptr;
      }

      // wl_output is owned by the registry, released on disconnect
      for (auto *out : outputs) {
        wl_output_destroy(out);
      }
      outputs.clear();

      if (registry) {
        wl_registry_destroy(registry);
        registry = nullptr;
      }

      if (display) {
        wl_display_disconnect(display);
        display = nullptr;
      }
    }

    /**
     * @brief Connect to KWin wayland, enumerate outputs, request a screencast stream.
     * @param output_index Which wl_output to capture (0 = first).
     * @return 0 on success, -1 on failure. On success, node_id and
     *         output width/height/x/y are populated.
     */
    int init() {
      // Try to setup permissions for zkde_screencast_unstable_v1
      screencast_permission_helper_t::setup();

      const char *wl_name = std::getenv("WAYLAND_DISPLAY");
      if (!wl_name) {
        BOOST_LOG(error) << "[kwingrab] WAYLAND_DISPLAY not set"sv;
        return -1;
      }

      display = wl_display_connect(wl_name);
      if (!display) {
        BOOST_LOG(error) << "[kwingrab] cannot connect to Wayland display: "sv << wl_name;
        return -1;
      }

      registry = wl_display_get_registry(display);
      wl_registry_add_listener(registry, &registry_listener, this);
      wl_display_roundtrip(display);

      if (!zkde_screencast) {
        if (screencast_permission_helper_t::is_newly_initialized()) {
          BOOST_LOG(error) << "[kwingrab] zkde_screencast_unstable_v1 not found in registry. "sv
                              "A new permission desktop file was automatically created but might now have been recognized yet. Try restarting."sv;
        } else {
          BOOST_LOG(error) << "[kwingrab] zkde_screencast_unstable_v1 not found in registry. Check "sv
                              "desktop file for sunshine binary or set KWIN_WAYLAND_NO_PERMISSION_CHECKS=1"sv;
        }
        return -1;
      }
      if (outputs.empty()) {
        BOOST_LOG(error) << "[kwingrab] no wl_output found"sv;
        return -1;
      }
      // We need a second roundtrip after binding outputs to get wl_output events
      wl_display_roundtrip(display);

      return 0;
    }

    std::vector<std::string> get_outputs() {
      struct output_params_t_ {
        int index;
        std::string name;
        int width;
        int height;
        int pos_x;
        int pos_y;
        int order = 0;
      };

      std::vector<output_params_t_> output_params_;
      for (int i = 0; i < outputs.size(); i++) {
        output_params_.emplace_back(i, output_names[i], output_widths[i], output_heights[i], output_x_positions[i], output_y_positions[i], get_order_for_output_name(output_names[i]));
      }
      std::ranges::sort(output_params_, [](const auto &a, const auto &b) {
        return a.order < b.order || a.pos_x < b.pos_x || a.pos_y < b.pos_y;
      });
      std::vector<std::string> output_names_;
      for (auto output_param : output_params_) {
        BOOST_LOG(info) << "[kwingrab] Found output "sv << output_param.index << ": "sv << output_param.name << " order: "sv << output_param.order << " position: "sv << output_param.pos_x << "x"sv << output_param.pos_y << " resolution: "sv << output_param.width << "x"sv << output_param.height;
        output_names_.emplace_back(output_param.name);
      }
      return output_names_;
    }

    /**
     * @brief Connect to KWin wayland, enumerate outputs, request a screencast stream.
     * @param output_name Which wl_output to capture.
     * @return 0 on success, -1 on failure. On success, node_id and
     *         output width/height/x/y are populated.
     */
    int start(const std::string &output_name) {
      int output_index = 0;
      if (!output_name.empty()) {
        for (int i = 0; i < outputs.size(); i++) {
          if (output_names[i] == output_name) {
            output_index = i;
            break;
          }
        }
      }

      // Request a stream for the chosen output with embedded cursor
      auto *target_output = outputs[output_index];
      stream = zkde_screencast_unstable_v1_stream_output(zkde_screencast, target_output, CURSOR_EMBEDDED);
      zkde_screencast_stream_unstable_v1_add_listener(stream, &stream_listener, this);

      // Dispatch until we get created/failed, with a 5s timeout
      auto deadline = std::chrono::steady_clock::now() + 5s;
      while (node_id == 0 && !failed && std::chrono::steady_clock::now() < deadline) {
        wl_display_flush(display);

        struct pollfd pfd = {};
        pfd.fd = wl_display_get_fd(display);
        pfd.events = POLLIN;

        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now()
        );
        if (remaining.count() <= 0) {
          break;
        }

        if (poll(&pfd, 1, remaining.count()) > 0 && (pfd.revents & POLLIN) && wl_display_dispatch(display) < 0) {
          BOOST_LOG(error) << "[kwingrab] wl_display_dispatch failed"sv;
          return -1;
        }
      }

      if (failed) {
        BOOST_LOG(error) << "[kwingrab] stream_output failed: "sv << error_msg;
        return -1;
      }
      if (node_id == 0) {
        BOOST_LOG(error) << "[kwingrab] timeout waiting for created event"sv;
        return -1;
      }

      BOOST_LOG(info) << "[kwingrab] stream created, PipeWire node "sv << node_id;

      // Get output dimensions from mode events
      out_width = output_widths[output_index];
      out_height = output_heights[output_index];
      out_pos_x = output_x_positions[output_index];
      out_pos_y = output_y_positions[output_index];

      if (out_width == 0 || out_height == 0) {
        BOOST_LOG(error) << "[kwingrab] could not determine output dimensions"sv;
        return -1;
      }

      BOOST_LOG(info) << "[kwingrab] Screencasting output "sv << output_index
                      << " name "sv << output_names[output_index]
                      << " position "sv << out_pos_x << "x"sv << out_pos_y
                      << " resolution "sv << out_width << "x"sv << out_height;
      return 0;
    }

    uint32_t node_id = 0;
    int out_width = 0;
    int out_height = 0;
    int out_pos_x = 0;
    int out_pos_y = 0;

  private:
    // ─── Wayland objects ───
    struct wl_display *display = nullptr;
    struct wl_registry *registry = nullptr;
    struct kde_output_order_v1 *kde_output_order = nullptr;
    struct zkde_screencast_unstable_v1 *zkde_screencast = nullptr;
    struct zkde_screencast_stream_unstable_v1 *stream = nullptr;
    std::vector<struct wl_output *> outputs;
    std::vector<int> output_widths;
    std::vector<int> output_heights;
    std::vector<int> output_x_positions;
    std::vector<int> output_y_positions;
    std::vector<std::string> output_names;
    std::vector<std::string> output_order;
    bool failed = false;
    std::string error_msg;

    // ─── Misc functions ───
    int get_order_for_output_name(const std::string &name) const {
      for (int i = 0; i < output_order.size(); i++) {
        if (output_order[i] == name) {
          return i;
        }
      }
      // If nothing matches move output to the end of the list (highest order)
      return output_order.size();
    }

    // ─── Registry listener ───
    static void on_registry_global(void *data, struct wl_registry *reg, const uint32_t name, const char *interface, const uint32_t version) {
      auto *self = static_cast<screencast_t *>(data);
      if (!std::strcmp(interface, kde_output_order_v1_interface.name)) {
        // Bind version 1
        uint32_t bind_ver = std::min(version, static_cast<uint32_t>(1));
        self->kde_output_order = static_cast<struct kde_output_order_v1 *>(
          wl_registry_bind(reg, name, &kde_output_order_v1_interface, bind_ver)
        );
        kde_output_order_v1_add_listener(self->kde_output_order, &output_order_listener, self);
        BOOST_LOG(info) << "[kwingrab] bound kde_output_order_v1 v"sv << bind_ver;
      } else if (!std::strcmp(interface, zkde_screencast_unstable_v1_interface.name)) {
        // Bind version 1 — we only use stream_output which is v1
        uint32_t bind_ver = std::min(version, static_cast<uint32_t>(1));
        self->zkde_screencast = static_cast<struct zkde_screencast_unstable_v1 *>(
          wl_registry_bind(reg, name, &zkde_screencast_unstable_v1_interface, bind_ver)
        );
        BOOST_LOG(info) << "[kwingrab] bound zkde_screencast_unstable_v1 v"sv << bind_ver;
      } else if (!std::strcmp(interface, wl_output_interface.name)) {
        auto *output = static_cast<struct wl_output *>(
          wl_registry_bind(reg, name, &wl_output_interface, std::min(version, static_cast<uint32_t>(4)))
        );
        wl_output_add_listener(output, &output_listener, self);
        self->outputs.emplace_back(output);
        self->output_widths.emplace_back(0);
        self->output_heights.emplace_back(0);
        self->output_x_positions.emplace_back(0);
        self->output_y_positions.emplace_back(0);
        self->output_names.emplace_back("");
      }
    }

    static void on_registry_global_remove(void *data [[maybe_unused]], struct wl_registry *reg [[maybe_unused]], uint32_t name [[maybe_unused]]) {
      // We don't handle output hot-unplug during init
    }

    static constexpr struct wl_registry_listener registry_listener = {
      .global = on_registry_global,
      .global_remove = on_registry_global_remove,
    };

    // ─── wl_output listener (for mode/dimensions) ───
    static void on_output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y, int32_t pw [[maybe_unused]], int32_t ph [[maybe_unused]], int32_t subpixel [[maybe_unused]], const char *make [[maybe_unused]], const char *model [[maybe_unused]], int32_t transform [[maybe_unused]]) {
      auto *self = static_cast<screencast_t *>(data);
      for (size_t i = 0; i < self->outputs.size(); i++) {
        if (self->outputs[i] == output) {
          self->output_x_positions[i] = x;
          self->output_y_positions[i] = y;
          break;
        }
      }
    }

    static void on_output_mode(void *data, struct wl_output *output, uint32_t flags, int32_t width, int32_t height, int32_t refresh [[maybe_unused]]) {
      if (!(flags & WL_OUTPUT_MODE_CURRENT)) {
        return;
      }

      auto *self = static_cast<screencast_t *>(data);
      for (size_t i = 0; i < self->outputs.size(); i++) {
        if (self->outputs[i] == output) {
          self->output_widths[i] = width;
          self->output_heights[i] = height;
          break;
        }
      }
    }

    static void on_output_done(void *data [[maybe_unused]], struct wl_output *output [[maybe_unused]]) {
      // Currently unused
    }

    static void on_output_scale(void *data [[maybe_unused]], struct wl_output *output [[maybe_unused]], int32_t factor [[maybe_unused]]) {
      // Currently unused
    }

    static void on_output_name(void *data, struct wl_output *output, const char *name) {
      auto *self = static_cast<screencast_t *>(data);
      for (size_t i = 0; i < self->outputs.size(); i++) {
        if (self->outputs[i] == output) {
          self->output_names[i] = name;
          break;
        }
      }
    }

    static void on_output_description(void *data [[maybe_unused]], struct wl_output *output [[maybe_unused]], const char *description [[maybe_unused]]) {
      // Currently unused
    }

    static constexpr struct wl_output_listener output_listener = {
      .geometry = on_output_geometry,
      .mode = on_output_mode,
      .done = on_output_done,
      .scale = on_output_scale,
      .name = on_output_name,
      .description = on_output_description,
    };

    // ─── ScreenCast stream listener ───
    static void on_stream_closed(void *data, struct zkde_screencast_stream_unstable_v1 *stream [[maybe_unused]]) {
      auto *self = static_cast<screencast_t *>(data);
      BOOST_LOG(warning) << "[kwingrab] stream closed by server"sv;
      self->failed = true;
      self->error_msg = "stream closed by server";
    }

    static void on_stream_created(void *data, struct zkde_screencast_stream_unstable_v1 *stream [[maybe_unused]], const uint32_t node) {
      auto *self = static_cast<screencast_t *>(data);
      self->node_id = node;
      BOOST_LOG(debug) << "[kwingrab] created event, node_id="sv << node;
    }

    static void on_stream_failed(void *data, struct zkde_screencast_stream_unstable_v1 *stream [[maybe_unused]], const char *err_msg) {
      auto *self = static_cast<screencast_t *>(data);
      self->failed = true;
      self->error_msg = err_msg ? err_msg : "unknown error";
      BOOST_LOG(error) << "[kwingrab] failed event: "sv << self->error_msg;
    }

    static constexpr struct zkde_screencast_stream_unstable_v1_listener stream_listener = {
      .closed = on_stream_closed,
      .created = on_stream_created,
      .failed = on_stream_failed,
    };

    // ─── Output order listener ───
    static void on_output_order_output(void *data, struct kde_output_order_v1 *kde_output_order_v1 [[maybe_unused]], const char *output_name) {
      auto *self = static_cast<screencast_t *>(data);
      self->output_order.emplace_back(output_name);
    }

    static void on_output_order_done(void *data [[maybe_unused]], struct kde_output_order_v1 *kde_output_order_v1 [[maybe_unused]]) {
      // Currently unused
    }

    static constexpr kde_output_order_v1_listener output_order_listener = {
      .output = on_output_order_output,
      .done = on_output_order_done,
    };
  };

  // ─── Display backend ─────────────────────────────────────────────────────────
  //
  // Orchestrates screencast_t + pipewire_t, provides the capture loop.

  class kwin_t: public pipewire::pipewire_display_t {
  public:
    int configure_stream(const std::string &display_name, int &out_pipewire_fd, int &out_pipewire_node, int &out_pos_x, int &out_pos_y, int &out_width, int &out_height) override {
      screencast = std::make_unique<screencast_t>();
      if (screencast->init() < 0) {
        return -1;
      }
      if (screencast->start(display_name) < 0) {
        return -1;
      }

      out_pos_x = screencast->out_pos_x;
      out_pos_y = screencast->out_pos_y;
      out_width = screencast->out_width;
      out_height = screencast->out_height;
      out_pipewire_fd = -1;  // KWin capture runs of the local pipewire core
      out_pipewire_node = screencast->node_id;
      return 0;
    }

    std::unique_ptr<screencast_t> screencast;
  };
}  // namespace kwin

// ─── Public API for misc.cpp ─────────────────────────────────────────────────

namespace platf {
  std::shared_ptr<display_t> kwin_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    if (!pipewire::pipewire_display_t::init_pipewire_and_check_hwdevice_type(hwdevice_type)) {
      BOOST_LOG(error) << "[kwingrab] Could not initialize pipewire-based display with the given hw device type."sv;
      return nullptr;
    }

    // Drop CAP_SYS_ADMIN so KWin's permission check can see and match the executable
    if (has_elevated_privileges()) {
      drop_elevated_privileges();
    }

    auto display = std::make_shared<kwin::kwin_t>();
    if (display->init(hwdevice_type, display_name, config)) {
      return nullptr;
    }

    return display;
  }

  std::vector<std::string> kwin_display_names() {
    if (has_elevated_privileges()) {
      // We're still in the probing phase of Sunshine startup. Dropping portal security early will break KMS.
      // Just return a dummy screen for now. Display re-enumeration after encoder probing will yield full result.
      std::vector<std::string> display_names;
      display_names.emplace_back("");
      return display_names;
    }

    const auto screencast = std::make_unique<kwin::screencast_t>();
    if (screencast->init() < 0) {
      BOOST_LOG(warning) << "[kwingrab] KWin ScreenCast protocol not available."sv;
      return {};
    }
    // Return output indices as display names
    return screencast->get_outputs();
  }

  bool kwin_available() {
    // Verify that we can connect to Wayland and find KWin
    // Do this by checking for the kde-output-order-v1 protocol
    // as it works regardless of CAP_SYS_ADMIN status
    const char *wl_name = std::getenv("WAYLAND_DISPLAY");
    if (!wl_name) {
      return false;
    }

    auto *display = wl_display_connect(wl_name);
    if (!display) {
      return false;
    }

    bool found_kwin = false;
    bool found_output = false;

    struct probe_data_t {
      bool *found_kwin;
      bool *found_output;
    };

    probe_data_t probe = {&found_kwin, &found_output};

    static const struct wl_registry_listener probe_listener = {
      .global = [](void *data, struct wl_registry *, uint32_t, const char *interface, uint32_t) {
        auto *p = static_cast<probe_data_t *>(data);
        if (!std::strcmp(interface, kde_output_order_v1_interface.name)) {
          *p->found_kwin = true;
        } else if (!std::strcmp(interface, wl_output_interface.name)) {
          *p->found_output = true;
        }
      },
      .global_remove = [](void *, struct wl_registry *, uint32_t) {
        // Unused as we're just probing for available methods
      },
    };

    auto *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &probe_listener, &probe);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    return found_kwin && found_output;
  }
}  // namespace platf
