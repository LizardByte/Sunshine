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
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

// lib includes
#include <pipewire/pipewire.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>

// generated protocol header
#include <zkde-screencast-unstable-v1.h>

// local includes
#include "cuda.h"
#include "graphics.h"
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
      if (screencast) {
        zkde_screencast_unstable_v1_destroy(screencast);
        screencast = nullptr;
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

      if (!screencast) {
        BOOST_LOG(error) << "[kwingrab] zkde_screencast_unstable_v1 not found in registry. "
                            "Is KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 set?"sv;
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
        int width;
        int height;
        int pos_x;
        int pos_y;
      };

      std::vector<output_params_t_> output_params_;
      for (int i = 0; i < outputs.size(); i++) {
        output_params_.emplace_back(i, output_widths[i], output_heights[i], output_x_positions[i], output_y_positions[i]);
      }
      // TODO: Add output priority from kde-output-device-v2 here as first sorting parameter
      std::ranges::sort(output_params_, [](const auto &a, const auto &b) {
        return a.pos_x < b.pos_x || a.pos_y < b.pos_y;
      });
      std::vector<std::string> output_names_;
      for (auto output_param : output_params_) {
        BOOST_LOG(info) << "[kwingrab] Found output: "sv << output_param.index << " position: "sv << output_param.pos_x << "x"sv << output_param.pos_y << " resolution: "sv << output_param.width << "x"sv << output_param.height;
        output_names_.emplace_back(std::to_string(output_param.index));
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
      if (!output_name.empty() && std::ranges::all_of(output_name, ::isdigit)) {
        output_index = std::stoi(output_name);
      } else {
        BOOST_LOG(debug) << "[kwingrab] Failed to parse int value: '"sv << output_name << "'";
      }

      // Request a stream for the chosen output with embedded cursor
      auto *target_output = outputs[output_index];
      stream = zkde_screencast_unstable_v1_stream_output(screencast, target_output, CURSOR_EMBEDDED);
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
    struct zkde_screencast_unstable_v1 *screencast = nullptr;
    struct zkde_screencast_stream_unstable_v1 *stream = nullptr;
    std::vector<struct wl_output *> outputs;
    std::vector<int> output_widths;
    std::vector<int> output_heights;
    std::vector<int> output_x_positions;
    std::vector<int> output_y_positions;
    bool failed = false;
    std::string error_msg;

    // ─── Registry listener ───
    static void on_registry_global(void *data, struct wl_registry *reg, const uint32_t name, const char *interface, const uint32_t version) {
      auto *self = static_cast<screencast_t *>(data);

      if (!std::strcmp(interface, zkde_screencast_unstable_v1_interface.name)) {
        // Bind version 1 — we only use stream_output which is v1
        uint32_t bind_ver = std::min(version, static_cast<uint32_t>(1));
        self->screencast = static_cast<struct zkde_screencast_unstable_v1 *>(
          wl_registry_bind(reg, name, &zkde_screencast_unstable_v1_interface, bind_ver)
        );
        BOOST_LOG(info) << "[kwingrab] bound zkde_screencast_unstable_v1 v"sv << bind_ver;
      } else if (!std::strcmp(interface, wl_output_interface.name)) {
        auto *output = static_cast<struct wl_output *>(
          wl_registry_bind(reg, name, &wl_output_interface, std::min(version, static_cast<uint32_t>(2)))
        );
        wl_output_add_listener(output, &output_listener, self);
        self->outputs.emplace_back(output);
        self->output_widths.emplace_back(0);
        self->output_heights.emplace_back(0);
        self->output_x_positions.emplace_back(0);
        self->output_y_positions.emplace_back(0);
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

    static constexpr struct wl_output_listener output_listener = {
      .geometry = on_output_geometry,
      .mode = on_output_mode,
      .done = on_output_done,
      .scale = on_output_scale,
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

    auto display = std::make_shared<kwin::kwin_t>();
    if (display->init(hwdevice_type, display_name, config)) {
      return nullptr;
    }

    return display;
  }

  std::vector<std::string> kwin_display_names() {
    const auto screencast = std::make_unique<kwin::screencast_t>();
    if (screencast->init() < 0) {
      BOOST_LOG(warning) << "[kwingrab] KWin ScreenCast protocol not available."sv;
      return {};
    }
    // Return output indices as display names
    return screencast->get_outputs();
  }
}  // namespace platf
