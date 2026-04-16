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
#include <libdrm/drm_fourcc.h>
#include <pipewire/pipewire.h>
#include <poll.h>
#include <unistd.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>
#include <spa/pod/builder.h>
#include <wayland-client.h>

// generated protocol header
#include <zkde-screencast-unstable-v1.h>

// local includes
#include "cuda.h"
#include "graphics.h"
#include "src/platform/common.h"
#include "src/platform/linux/pipewire.cpp"
#include "src/video.h"
#include "vaapi.h"
#include "wayland.h"

namespace {
  // KDE ScreenCast cursor modes (from protocol enum)
  constexpr uint32_t CURSOR_HIDDEN = 1;
  constexpr uint32_t CURSOR_EMBEDDED = 2;
  constexpr uint32_t CURSOR_METADATA = 4;
}  // namespace

using namespace std::literals;

extern const wl_interface wl_output_interface;

namespace kwin {

  struct format_map_t {
    uint64_t fourcc;
    int32_t pw_format;
  };

  static constexpr std::array<format_map_t, 3> format_map = {{
    {DRM_FORMAT_ARGB8888, SPA_VIDEO_FORMAT_BGRA},
    {DRM_FORMAT_XRGB8888, SPA_VIDEO_FORMAT_BGRx},
    {0, 0},
  }};

  struct stream_data_t {
    struct pw_stream *stream;
    struct spa_hook stream_listener;
    struct spa_video_info format;
    struct pw_buffer *current_buffer;
    uint64_t drm_format;
  };

  struct dmabuf_format_info_t {
    int32_t format;
    uint64_t *modifiers;
    int n_modifiers;
  };

  // ─── Wayland ScreenCast session ──────────────────────────────────────────────
  //
  // Owns its own wl_display connection. Binds zkde_screencast_unstable_v1
  // and wl_output from the registry, then calls stream_output() to start
  // a ScreenCast. Waits for the created(node_id) event from KWin.

  class screencast_t {
  public:
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
     * @brief Connect to KWin, enumerate outputs, request a screencast stream.
     * @param output_index Which wl_output to capture (0 = first).
     * @return 0 on success, -1 on failure. On success, node_id and
     *         output width/height are populated.
     */
    int init(int output_index = 0) {
      const char *wl_name = std::getenv("WAYLAND_DISPLAY");
      if (!wl_name) {
        BOOST_LOG(error) << "WAYLAND_DISPLAY not set"sv;
        return -1;
      }

      display = wl_display_connect(wl_name);
      if (!display) {
        BOOST_LOG(error) << "KWin ScreenCast: cannot connect to Wayland display: "sv << wl_name;
        return -1;
      }

      registry = wl_display_get_registry(display);
      wl_registry_add_listener(registry, &registry_listener, this);
      wl_display_roundtrip(display);

      if (!screencast) {
        BOOST_LOG(error) << "KWin ScreenCast: zkde_screencast_unstable_v1 not found in registry. "
                            "Is KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 set?"sv;
        return -1;
      }
      if (outputs.empty()) {
        BOOST_LOG(error) << "KWin ScreenCast: no wl_output found"sv;
        return -1;
      }
      if (output_index < 0 || output_index >= static_cast<int>(outputs.size())) {
        BOOST_LOG(error) << "KWin ScreenCast: output index "sv << output_index
                         << " out of range (have "sv << outputs.size() << " outputs)"sv;
        return -1;
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
          deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;

        if (poll(&pfd, 1, remaining.count()) > 0 && (pfd.revents & POLLIN)) {
          if (wl_display_dispatch(display) < 0) {
            BOOST_LOG(error) << "KWin ScreenCast: wl_display_dispatch failed"sv;
            return -1;
          }
        }
      }

      if (failed) {
        BOOST_LOG(error) << "KWin ScreenCast: stream_output failed: "sv << error_msg;
        return -1;
      }
      if (node_id == 0) {
        BOOST_LOG(error) << "KWin ScreenCast: timeout waiting for created event"sv;
        return -1;
      }

      BOOST_LOG(info) << "KWin ScreenCast: stream created, PipeWire node "sv << node_id;

      // Get output dimensions from mode events
      // We need a second roundtrip after binding outputs to get mode events
      wl_display_roundtrip(display);
      out_width = output_widths[output_index];
      out_height = output_heights[output_index];

      if (out_width == 0 || out_height == 0) {
        BOOST_LOG(warning) << "KWin ScreenCast: could not determine output dimensions, using defaults"sv;
        out_width = 1920;
        out_height = 1080;
      }

      BOOST_LOG(info) << "KWin ScreenCast: output "sv << output_index
                       << " resolution "sv << out_width << "x"sv << out_height;

      return 0;
    }

    uint32_t node_id = 0;
    int out_width = 0;
    int out_height = 0;

  private:
    // ─── Wayland objects ───
    struct wl_display *display = nullptr;
    struct wl_registry *registry = nullptr;
    struct zkde_screencast_unstable_v1 *screencast = nullptr;
    struct zkde_screencast_stream_unstable_v1 *stream = nullptr;
    std::vector<struct wl_output *> outputs;
    std::vector<int> output_widths;
    std::vector<int> output_heights;
    bool failed = false;
    std::string error_msg;

    // ─── Registry listener ───
    static void on_registry_global(void *data, struct wl_registry *reg,
                                   uint32_t name, const char *interface, uint32_t version) {
      auto *self = static_cast<screencast_t *>(data);

      if (!std::strcmp(interface, zkde_screencast_unstable_v1_interface.name)) {
        // Bind version 1 — we only use stream_output which is v1
        uint32_t bind_ver = std::min(version, static_cast<uint32_t>(1));
        self->screencast = static_cast<struct zkde_screencast_unstable_v1 *>(
          wl_registry_bind(reg, name, &zkde_screencast_unstable_v1_interface, bind_ver));
        BOOST_LOG(info) << "KWin ScreenCast: bound zkde_screencast_unstable_v1 v"sv << bind_ver;
      }
      else if (!std::strcmp(interface, wl_output_interface.name)) {
        auto *output = static_cast<struct wl_output *>(
          wl_registry_bind(reg, name, &wl_output_interface, std::min(version, static_cast<uint32_t>(2))));
        wl_output_add_listener(output, &output_listener, self);
        self->outputs.push_back(output);
        self->output_widths.push_back(0);
        self->output_heights.push_back(0);
      }
    }

    static void on_registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
      // We don't handle output hot-unplug during init
    }

    static constexpr struct wl_registry_listener registry_listener = {
      .global = on_registry_global,
      .global_remove = on_registry_global_remove,
    };

    // ─── wl_output listener (for mode/dimensions) ───
    static void on_output_geometry(void *data, struct wl_output *output,
                                   int32_t x, int32_t y, int32_t pw, int32_t ph,
                                   int32_t subpixel, const char *make,
                                   const char *model, int32_t transform) {}

    static void on_output_mode(void *data, struct wl_output *output,
                               uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
      if (!(flags & WL_OUTPUT_MODE_CURRENT)) return;

      auto *self = static_cast<screencast_t *>(data);
      for (size_t i = 0; i < self->outputs.size(); i++) {
        if (self->outputs[i] == output) {
          self->output_widths[i] = width;
          self->output_heights[i] = height;
          break;
        }
      }
    }

    static void on_output_done(void *data, struct wl_output *output) {}
    static void on_output_scale(void *data, struct wl_output *output, int32_t factor) {}

    static constexpr struct wl_output_listener output_listener = {
      .geometry = on_output_geometry,
      .mode = on_output_mode,
      .done = on_output_done,
      .scale = on_output_scale,
    };

    // ─── ScreenCast stream listener ───
    static void on_stream_closed(void *data, struct zkde_screencast_stream_unstable_v1 *stream) {
      auto *self = static_cast<screencast_t *>(data);
      BOOST_LOG(warning) << "KWin ScreenCast: stream closed by server"sv;
      self->failed = true;
      self->error_msg = "stream closed by server";
    }

    static void on_stream_created(void *data, struct zkde_screencast_stream_unstable_v1 *stream, uint32_t node) {
      auto *self = static_cast<screencast_t *>(data);
      self->node_id = node;
      BOOST_LOG(debug) << "KWin ScreenCast: created event, node_id="sv << node;
    }

    static void on_stream_failed(void *data, struct zkde_screencast_stream_unstable_v1 *stream, const char *err_msg) {
      auto *self = static_cast<screencast_t *>(data);
      self->failed = true;
      self->error_msg = err_msg ? err_msg : "unknown error";
      BOOST_LOG(error) << "KWin ScreenCast: failed event: "sv << self->error_msg;
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
       // Parse output index from display_name (default 0)
      int output_index = 0;
      if (!display_name.empty()) {
        try {
          output_index = std::stoi(display_name);
        } catch (...) {
          output_index = 0;
        }
      }

      // Connect to KWin ScreenCast
      screencast = std::make_unique<screencast_t>();
      if (screencast->init(output_index) < 0) {
        return -1;
      }

      out_pos_x = 0; // TODO: Implment multi-monitor
      out_pos_y = 0; // TODO: Implment multi-monitor
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
    // Verify that we can connect to Wayland and find the ScreenCast protocol
    const char *wl_name = std::getenv("WAYLAND_DISPLAY");
    if (!wl_name) {
      return {};
    }

    auto *display = wl_display_connect(wl_name);
    if (!display) {
      return {};
    }

    bool found_screencast = false;
    bool found_output = false;

    struct probe_data_t {
      bool *found_screencast;
      bool *found_output;
    } probe = {&found_screencast, &found_output};

    static const struct wl_registry_listener probe_listener = {
      .global = [](void *data, struct wl_registry *, uint32_t, const char *interface, uint32_t) {
        auto *p = static_cast<probe_data_t *>(data);
        if (!std::strcmp(interface, zkde_screencast_unstable_v1_interface.name)) {
          *p->found_screencast = true;
        } else if (!std::strcmp(interface, wl_output_interface.name)) {
          *p->found_output = true;
        }
      },
      .global_remove = [](void *, struct wl_registry *, uint32_t) {},
    };

    auto *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &probe_listener, &probe);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    if (!found_screencast) {
      BOOST_LOG(warning) << "[kwingrab] KWin ScreenCast protocol not available."sv;
      return {};
    }
    if (!found_output) {
      return {};
    }

    // Return output indices as display names
    // TODO: Implement multi-monitor
    std::vector<std::string> names;
    names.emplace_back("0");
    return names;
  }
}  // namespace platf
