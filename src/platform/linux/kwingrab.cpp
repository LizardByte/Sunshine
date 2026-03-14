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
#include "src/main.h"
#include "src/platform/common.h"
#include "src/video.h"
#include "vaapi.h"
#include "wayland.h"

namespace {
  constexpr int SPA_POD_BUFFER_SIZE = 4096;
  constexpr int MAX_PARAMS = 200;
  constexpr int MAX_DMABUF_FORMATS = 200;
  constexpr int MAX_DMABUF_MODIFIERS = 200;

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

  // ─── PipeWire stream ─────────────────────────────────────────────────────────
  //
  // Connects to the local PipeWire daemon (pw_context_connect, NOT
  // pw_context_connect_fd). Receives frames as DMA-BUF or memory from
  // the PipeWire node created by KWin's ScreenCast plugin.
  //
  // This is structurally identical to portal::pipewire_t from portalgrab.cpp,
  // with the single difference that we use a local PipeWire connection.

  class pipewire_t {
  public:
    pipewire_t():
        loop(pw_thread_loop_new("KWin PipeWire", nullptr)) {
      pw_thread_loop_start(loop);
    }

    ~pipewire_t() {
      pw_thread_loop_lock(loop);

      if (stream_data.stream) {
        pw_stream_destroy(stream_data.stream);
        stream_data.stream = nullptr;
      }
      if (core) {
        pw_core_disconnect(core);
        core = nullptr;
      }
      if (context) {
        pw_context_destroy(context);
        context = nullptr;
      }

      pw_thread_loop_unlock(loop);

      pw_thread_loop_stop(loop);
      pw_thread_loop_destroy(loop);
    }

    /**
     * @brief Connect to local PipeWire daemon and target a specific node.
     * @param stream_node PipeWire node_id from KWin's created event.
     */
    void init(uint32_t stream_node) {
      node = stream_node;

      pw_thread_loop_lock(loop);

      context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
      if (context) {
        core = pw_context_connect(context, nullptr, 0);
        if (core) {
          pw_core_add_listener(core, &core_listener, &core_events, nullptr);
        } else {
          BOOST_LOG(error) << "KWin PipeWire: pw_context_connect failed"sv;
        }
      } else {
        BOOST_LOG(error) << "KWin PipeWire: pw_context_new failed"sv;
      }

      pw_thread_loop_unlock(loop);
    }

    void ensure_stream(const platf::mem_type_e mem_type, const uint32_t width, const uint32_t height,
                       const uint32_t refresh_rate, const struct dmabuf_format_info_t *dmabuf_infos,
                       const int n_dmabuf_infos, const bool display_is_nvidia) {
      pw_thread_loop_lock(loop);
      if (!stream_data.stream) {
        struct pw_properties *props = pw_properties_new(
          PW_KEY_MEDIA_TYPE, "Video",
          PW_KEY_MEDIA_CATEGORY, "Capture",
          PW_KEY_MEDIA_ROLE, "Screen",
          nullptr);

        stream_data.stream = pw_stream_new(core, "Sunshine KWin Capture", props);
        pw_stream_add_listener(stream_data.stream, &stream_data.stream_listener, &stream_events, &stream_data);

        std::array<uint8_t, SPA_POD_BUFFER_SIZE> buffer;
        struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());

        int n_params = 0;
        std::array<const struct spa_pod *, MAX_PARAMS> params;

        // DMA-BUF formats with modifiers (preferred for zero-copy)
        bool use_dmabuf = n_dmabuf_infos > 0 && (mem_type == platf::mem_type_e::vaapi ||
                                                   (mem_type == platf::mem_type_e::cuda && display_is_nvidia));
        if (use_dmabuf) {
          for (int i = 0; i < n_dmabuf_infos; i++) {
            auto format_param = build_format_parameter(&pod_builder, width, height, refresh_rate,
              dmabuf_infos[i].format, dmabuf_infos[i].modifiers, dmabuf_infos[i].n_modifiers);
            params[n_params++] = format_param;
          }
        }

        // Memory buffer fallback
        for (const auto &fmt : format_map) {
          if (fmt.fourcc == 0) break;
          auto format_param = build_format_parameter(&pod_builder, width, height, refresh_rate, fmt.pw_format, nullptr, 0);
          params[n_params++] = format_param;
        }

        pw_stream_connect(stream_data.stream, PW_DIRECTION_INPUT, node,
          static_cast<enum pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
          params.data(), n_params);
      }
      pw_thread_loop_unlock(loop);
    }

    void fill_img(platf::img_t *img) {
      pw_thread_loop_lock(loop);

      if (stream_data.current_buffer) {
        struct spa_buffer *buf = stream_data.current_buffer->buffer;
        if (buf->datas[0].chunk->size != 0) {
          const auto img_descriptor = static_cast<egl::img_descriptor_t *>(img);
          img_descriptor->frame_timestamp = std::chrono::steady_clock::now();
          if (buf->datas[0].type == SPA_DATA_DmaBuf) {
            img_descriptor->sd.width = stream_data.format.info.raw.size.width;
            img_descriptor->sd.height = stream_data.format.info.raw.size.height;
            img_descriptor->sd.modifier = stream_data.format.info.raw.modifier;
            img_descriptor->sd.fourcc = stream_data.drm_format;

            for (int i = 0; i < std::min(buf->n_datas, static_cast<uint32_t>(4)); i++) {
              img_descriptor->sd.fds[i] = dup(buf->datas[i].fd);
              img_descriptor->sd.pitches[i] = buf->datas[i].chunk->stride;
              img_descriptor->sd.offsets[i] = buf->datas[i].chunk->offset;
            }
          } else {
            img->data = static_cast<std::uint8_t *>(buf->datas[0].data);
            img->row_pitch = buf->datas[0].chunk->stride;
          }
        }
      }

      pw_thread_loop_unlock(loop);
    }

  private:
    struct pw_thread_loop *loop;
    struct pw_context *context = nullptr;
    struct pw_core *core = nullptr;
    struct spa_hook core_listener;
    struct stream_data_t stream_data = {};
    uint32_t node = 0;

    static struct spa_pod *build_format_parameter(struct spa_pod_builder *b,
        uint32_t width, uint32_t height, uint32_t refresh_rate,
        int32_t format, uint64_t *modifiers, int n_modifiers) {
      struct spa_pod_frame object_frame;
      struct spa_pod_frame modifier_frame;
      std::array<struct spa_rectangle, 3> sizes;
      std::array<struct spa_fraction, 3> framerates;

      sizes[0] = SPA_RECTANGLE(width, height);
      sizes[1] = SPA_RECTANGLE(1, 1);
      sizes[2] = SPA_RECTANGLE(8192, 4096);

      // Variable rate — we want frames as fast as KWin produces them
      framerates[0] = SPA_FRACTION(0, 1);
      framerates[1] = SPA_FRACTION(0, 1);
      framerates[2] = SPA_FRACTION(0, 1);

      spa_pod_builder_push_object(b, &object_frame, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
      spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
      spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&sizes[0], &sizes[1], &sizes[2]), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&framerates[0], &framerates[1], &framerates[2]), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction(&framerates[0], &framerates[1], &framerates[2]), 0);

      if (n_modifiers) {
        spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        spa_pod_builder_push_choice(b, &modifier_frame, SPA_CHOICE_Enum, 0);
        spa_pod_builder_long(b, modifiers[0]);
        for (int i = 0; i < n_modifiers; i++) {
          spa_pod_builder_long(b, modifiers[i]);
        }
        spa_pod_builder_pop(b, &modifier_frame);
      }

      return static_cast<struct spa_pod *>(spa_pod_builder_pop(b, &object_frame));
    }

    static void on_core_info_cb([[maybe_unused]] void *user_data, const struct pw_core_info *pw_info) {
      BOOST_LOG(info) << "KWin PipeWire: connected to PipeWire "sv << pw_info->version;
    }

    static void on_core_error_cb([[maybe_unused]] void *user_data, uint32_t id, int seq, [[maybe_unused]] int res, const char *message) {
      BOOST_LOG(error) << "KWin PipeWire: error id:"sv << id << " seq:"sv << seq << " message: "sv << message;
    }

    constexpr static const struct pw_core_events core_events = {
      .version = PW_VERSION_CORE_EVENTS,
      .info = on_core_info_cb,
      .error = on_core_error_cb,
    };

    static void on_process(void *user_data) {
      auto *d = static_cast<struct stream_data_t *>(user_data);
      struct pw_buffer *b = nullptr;

      // Drain all available buffers, keep only the latest
      while (true) {
        struct pw_buffer *aux = pw_stream_dequeue_buffer(d->stream);
        if (!aux) break;
        if (b) {
          pw_stream_queue_buffer(d->stream, b);
        }
        b = aux;
      }

      if (!b) {
        BOOST_LOG(warning) << "KWin PipeWire: out of buffers"sv;
        return;
      }

      if (d->current_buffer) {
        pw_stream_queue_buffer(d->stream, d->current_buffer);
      }
      d->current_buffer = b;
    }

    static void on_param_changed(void *user_data, uint32_t id, const struct spa_pod *param) {
      auto *d = static_cast<struct stream_data_t *>(user_data);

      d->current_buffer = nullptr;

      if (!param || id != SPA_PARAM_Format) return;
      if (spa_format_parse(param, &d->format.media_type, &d->format.media_subtype) < 0) return;
      if (d->format.media_type != SPA_MEDIA_TYPE_video || d->format.media_subtype != SPA_MEDIA_SUBTYPE_raw) return;
      if (spa_format_video_raw_parse(param, &d->format.info.raw) < 0) return;

      BOOST_LOG(info) << "KWin PipeWire: format "sv << d->format.info.raw.format
                       << " size "sv << d->format.info.raw.size.width << "x"sv << d->format.info.raw.size.height;

      uint64_t drm_format = 0;
      for (const auto &fmt : format_map) {
        if (fmt.fourcc == 0) break;
        if (fmt.pw_format == d->format.info.raw.format) {
          drm_format = fmt.fourcc;
        }
      }
      d->drm_format = drm_format;

      uint32_t buffer_types = 0;
      if (spa_pod_find_prop(param, nullptr, SPA_FORMAT_VIDEO_modifier) != nullptr && d->drm_format) {
        BOOST_LOG(info) << "KWin PipeWire: using DMA-BUF buffers"sv;
        buffer_types |= 1 << SPA_DATA_DmaBuf;
      } else {
        BOOST_LOG(info) << "KWin PipeWire: using memory buffers"sv;
        buffer_types |= 1 << SPA_DATA_MemPtr;
      }

      std::array<uint8_t, SPA_POD_BUFFER_SIZE> buffer;
      std::array<const struct spa_pod *, 1> params;
      int n_params = 0;
      struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());
      auto buffer_param = static_cast<const struct spa_pod *>(
        spa_pod_builder_add_object(&pod_builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
          SPA_PARAM_BUFFERS_dataType, SPA_POD_Int(buffer_types)));
      params[n_params++] = buffer_param;
      pw_stream_update_params(d->stream, params.data(), n_params);
    }

    constexpr static const struct pw_stream_events stream_events = {
      .version = PW_VERSION_STREAM_EVENTS,
      .param_changed = on_param_changed,
      .process = on_process,
    };
  };

  // ─── Display backend ─────────────────────────────────────────────────────────
  //
  // Orchestrates screencast_t + pipewire_t, provides the capture loop.

  class kwin_t: public platf::display_t {
  public:
    int init(platf::mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config) {
      framerate = config.framerate;
      delay = std::chrono::nanoseconds {1s} / framerate;
      mem_type = hwdevice_type;

      if (get_dmabuf_modifiers() < 0) {
        return -1;
      }

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

      width = screencast->out_width;
      height = screencast->out_height;

      // Connect to PipeWire with the node from KWin
      pw_init(nullptr, nullptr);
      pipewire = std::make_unique<pipewire_t>();
      pipewire->init(screencast->node_id);

      return 0;
    }

    platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb,
                              std::shared_ptr<platf::img_t> &img_out,
                              std::chrono::milliseconds timeout, bool show_cursor) {
      if (!pull_free_image_cb(img_out)) {
        return platf::capture_e::interrupted;
      }

      auto img_egl = static_cast<egl::img_descriptor_t *>(img_out.get());
      img_egl->reset();
      pipewire->fill_img(img_egl);

      if (img_egl->sd.fds[0] < 0 && img_egl->data == nullptr) {
        return platf::capture_e::timeout;
      }

      img_egl->sequence = ++sequence;
      return platf::capture_e::ok;
    }

    std::shared_ptr<platf::img_t> alloc_img() override {
      auto img = std::make_shared<egl::img_descriptor_t>();

      img->width = width;
      img->height = height;
      img->pixel_pitch = 4;
      img->row_pitch = img->pixel_pitch * width;
      img->sequence = 0;
      img->serial = std::numeric_limits<decltype(img->serial)>::max();
      img->data = nullptr;
      std::fill_n(img->sd.fds, 4, -1);

      return img;
    }

    platf::capture_e capture(const push_captured_image_cb_t &push_captured_image_cb,
                             const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      auto next_frame = std::chrono::steady_clock::now();

      pipewire->ensure_stream(mem_type, width, height, framerate, dmabuf_infos.data(), n_dmabuf_infos, display_is_nvidia);
      sleep_overshoot_logger.reset();

      while (true) {
        auto now = std::chrono::steady_clock::now();

        if (next_frame > now) {
          std::this_thread::sleep_for(next_frame - now);
          sleep_overshoot_logger.first_point(next_frame);
          sleep_overshoot_logger.second_point_now_and_log();
        }

        next_frame += delay;
        if (next_frame < now) {
          next_frame = now + delay;
        }

        std::shared_ptr<platf::img_t> img_out;
        switch (const auto status = snapshot(pull_free_image_cb, img_out, 1000ms, *cursor)) {
          case platf::capture_e::reinit:
          case platf::capture_e::error:
          case platf::capture_e::interrupted:
            return status;
          case platf::capture_e::timeout:
            push_captured_image_cb(std::move(img_out), false);
            break;
          case platf::capture_e::ok:
            push_captured_image_cb(std::move(img_out), true);
            break;
          default:
            BOOST_LOG(error) << "KWin capture: unrecognized status ["sv << std::to_underlying(status) << ']';
            return status;
        }
      }

      return platf::capture_e::ok;
    }

    std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(platf::pix_fmt_e pix_fmt) override {
#ifdef SUNSHINE_BUILD_VAAPI
      if (mem_type == platf::mem_type_e::vaapi) {
        return va::make_avcodec_encode_device(width, height, n_dmabuf_infos > 0);
      }
#endif

#ifdef SUNSHINE_BUILD_CUDA
      if (mem_type == platf::mem_type_e::cuda) {
        if (display_is_nvidia && n_dmabuf_infos > 0) {
          return cuda::make_avcodec_gl_encode_device(width, height, 0, 0);
        } else {
          return cuda::make_avcodec_encode_device(width, height, false);
        }
      }
#endif

      return std::make_unique<platf::avcodec_encode_device_t>();
    }

    int dummy_img(platf::img_t *img) override {
      if (!img) return -1;

      img->data = new std::uint8_t[img->height * img->row_pitch];
      std::fill_n(img->data, img->height * img->row_pitch, 0);
      return 0;
    }

  private:
    static uint32_t lookup_pw_format(uint64_t fourcc) {
      for (const auto &fmt : format_map) {
        if (fmt.fourcc == 0) break;
        if (fmt.fourcc == fourcc) return fmt.pw_format;
      }
      return 0;
    }

    void query_dmabuf_formats(EGLDisplay egl_display) {
      EGLint num_dmabuf_formats = 0;
      std::array<EGLint, MAX_DMABUF_FORMATS> dmabuf_formats = {0};
      eglQueryDmaBufFormatsEXT(egl_display, MAX_DMABUF_FORMATS, dmabuf_formats.data(), &num_dmabuf_formats);

      if (num_dmabuf_formats > MAX_DMABUF_FORMATS) {
        BOOST_LOG(warning) << "Some DMA-BUF formats are being ignored"sv;
      }

      for (EGLint i = 0; i < std::min(static_cast<int>(num_dmabuf_formats), MAX_DMABUF_FORMATS); i++) {
        uint32_t pw_format = lookup_pw_format(dmabuf_formats[i]);
        if (pw_format == 0) continue;

        EGLint num_modifiers = 0;
        std::array<EGLuint64KHR, MAX_DMABUF_MODIFIERS> mods = {0};
        EGLBoolean external_only;
        eglQueryDmaBufModifiersEXT(egl_display, dmabuf_formats[i], MAX_DMABUF_MODIFIERS, mods.data(), &external_only, &num_modifiers);

        if (num_modifiers > MAX_DMABUF_MODIFIERS) {
          BOOST_LOG(warning) << "Some DMA-BUF modifiers are being ignored"sv;
        }

        dmabuf_infos[n_dmabuf_infos].format = pw_format;
        dmabuf_infos[n_dmabuf_infos].n_modifiers = std::min(static_cast<int>(num_modifiers), MAX_DMABUF_MODIFIERS);
        dmabuf_infos[n_dmabuf_infos].modifiers =
          static_cast<uint64_t *>(malloc(sizeof(uint64_t) * dmabuf_infos[n_dmabuf_infos].n_modifiers));
        std::memcpy(dmabuf_infos[n_dmabuf_infos].modifiers, mods.data(),
          sizeof(uint64_t) * dmabuf_infos[n_dmabuf_infos].n_modifiers);
        ++n_dmabuf_infos;
      }
    }

    int get_dmabuf_modifiers() {
      if (wl_display.init() < 0) {
        return -1;
      }

      auto egl_display = egl::make_display(wl_display.get());
      if (!egl_display) {
        return -1;
      }

      // Pure NVIDIA detection — on our dedicated NVIDIA container there is no Intel GPU
      const char *vendor = eglQueryString(egl_display.get(), EGL_VENDOR);
      if (vendor && std::string_view(vendor).contains("NVIDIA")) {
        BOOST_LOG(info) << "KWin capture: NVIDIA EGL display — DMA-BUF enabled for CUDA"sv;
        display_is_nvidia = true;
      } else {
        // Check for hybrid GPU
        auto check_intel = [](const std::string &path) {
          if (std::ifstream f(path); f.good()) {
            std::string v;
            f >> v;
            return v == "0x8086";
          }
          return false;
        };
        if (check_intel("/sys/class/drm/card0/device/vendor") ||
            check_intel("/sys/class/drm/card1/device/vendor")) {
          BOOST_LOG(info) << "KWin capture: hybrid GPU detected — CUDA will use memory buffers"sv;
          display_is_nvidia = false;
        }
      }

      if (eglQueryDmaBufFormatsEXT && eglQueryDmaBufModifiersEXT) {
        query_dmabuf_formats(egl_display.get());
      }

      return 0;
    }

    platf::mem_type_e mem_type;
    wl::display_t wl_display;
    std::unique_ptr<screencast_t> screencast;
    std::unique_ptr<pipewire_t> pipewire;
    std::array<struct dmabuf_format_info_t, MAX_DMABUF_FORMATS> dmabuf_infos = {};
    int n_dmabuf_infos = 0;
    bool display_is_nvidia = false;
    std::chrono::nanoseconds delay;
    std::uint64_t sequence {};
    uint32_t framerate;
  };
}  // namespace kwin

// ─── Public API for misc.cpp ─────────────────────────────────────────────────

namespace platf {
  std::shared_ptr<display_t> kwin_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config) {
    using enum platf::mem_type_e;
    if (hwdevice_type != system && hwdevice_type != vaapi && hwdevice_type != cuda) {
      BOOST_LOG(error) << "KWin capture: unsupported hw device type"sv;
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
      BOOST_LOG(debug) << "KWin ScreenCast protocol not available"sv;
      return {};
    }
    if (!found_output) {
      return {};
    }

    pw_init(nullptr, nullptr);

    // Return output indices as display names
    std::vector<std::string> names;
    names.emplace_back("0");
    return names;
  }
}  // namespace platf
