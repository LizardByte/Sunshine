/**
 * @file src/platform/linux/pipewire.cpp
 * @brief Shared classes for pipewire-based capture methods.
 */
// standard includes
#include <fstream>

// lib includes
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <libdrm/drm_fourcc.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>
#include <spa/pod/builder.h>

// local includes
#include "cuda.h"
#include "graphics.h"
#include "src/main.h"
#include "src/platform/common.h"
#include "src/video.h"
#include "vaapi.h"
#include "vulkan_encode.h"
#include "wayland.h"

namespace {
  // Buffer and limit constants
  constexpr int SPA_POD_BUFFER_SIZE = 4096;
  constexpr int MAX_PARAMS = 200;
  constexpr int MAX_DMABUF_FORMATS = 200;
  constexpr int MAX_DMABUF_MODIFIERS = 200;
}  // namespace

using namespace std::literals;

namespace pipewire {
  struct format_map_t {
    uint64_t fourcc;
    int32_t pw_format;
  };

  static constexpr std::array<format_map_t, 3> format_map = {{
    {DRM_FORMAT_ARGB8888, SPA_VIDEO_FORMAT_BGRA},
    {DRM_FORMAT_XRGB8888, SPA_VIDEO_FORMAT_BGRx},
    {0, 0},
  }};

  struct shared_state_t {
    std::atomic<int> negotiated_width {0};
    std::atomic<int> negotiated_height {0};
    std::atomic<bool> stream_dead {false};
    pw_stream_state previous_state;
    pw_stream_state current_state;
    std::string err_msg;
  };

  struct stream_data_t {
    struct pw_stream *stream;
    struct spa_hook stream_listener;
    struct spa_video_info format;
    struct pw_buffer *current_buffer;
    uint64_t drm_format;
    std::shared_ptr<shared_state_t> shared;
    std::mutex frame_mutex;
    std::condition_variable frame_cv;
    size_t local_stride = 0;
    bool frame_ready = false;
    // Two distinct memory pools
    std::vector<uint8_t> buffer_a;
    std::vector<uint8_t> buffer_b;
    // Points to the buffer currently owned by fill_img
    std::vector<uint8_t> *front_buffer;
    // Points to the buffer currently being written by on_process
    std::vector<uint8_t> *back_buffer;

    stream_data_t():
        front_buffer(&buffer_a),
        back_buffer(&buffer_b) {}
  };

  struct dmabuf_format_info_t {
    int32_t format;
    uint64_t *modifiers;
    int n_modifiers;
  };

  class pipewire_t {
  public:
    pipewire_t():
        loop(pw_thread_loop_new("Pipewire thread", nullptr)) {
      BOOST_LOG(debug) << "[pipewire] Start PW thread loop"sv;
      pw_thread_loop_start(loop);
    }

    ~pipewire_t() {
      BOOST_LOG(debug) << "[pipewire] Destroying pipewire_t"sv;
      if (loop) {
        BOOST_LOG(debug) << "[pipewire] Stop PW thread loop"sv;
        pw_thread_loop_stop(loop);
      }
      try {
        cleanup_stream();
      } catch (const std::exception &e) {
        BOOST_LOG(error) << "[pipewire] Standard exception caught in ~pipewire_t: "sv << e.what();
      } catch (...) {
        BOOST_LOG(error) << "[pipewire] Unknown exception caught in ~pipewire_t"sv;
      }

      pw_thread_loop_lock(loop);

      if (core) {
        BOOST_LOG(debug) << "[pipewire] Disconnect PW core"sv;
        pw_core_disconnect(core);
        core = nullptr;
      }
      if (context) {
        BOOST_LOG(debug) << "[pipewire] Destroy PW context"sv;
        pw_context_destroy(context);
        context = nullptr;
      }

      pw_thread_loop_unlock(loop);

      if (fd >= 0) {
        BOOST_LOG(debug) << "[pipewire] Close pipewire_fd"sv;
        close(fd);
      }
      BOOST_LOG(debug) << "[pipewire] Stop PW thread loop"sv;
      pw_thread_loop_stop(loop);
      BOOST_LOG(debug) << "[pipewire] Destroy PW thread loop"sv;
      pw_thread_loop_destroy(loop);
    }

    std::mutex &frame_mutex() {
      return stream_data.frame_mutex;
    }

    std::condition_variable &frame_cv() {
      return stream_data.frame_cv;
    }

    bool is_frame_ready() const {
      return stream_data.frame_ready;
    }

    void set_frame_ready(bool ready) {
      stream_data.frame_ready = ready;
    }

    int init(int stream_fd, int stream_node, std::shared_ptr<shared_state_t> shared_state) {
      fd = stream_fd;
      node = stream_node;
      stream_data.shared = std::move(shared_state);

      pw_thread_loop_lock(loop);
      BOOST_LOG(debug) << "[pipewire] Setup PW context"sv;
      context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
      if (context) {
        BOOST_LOG(debug) << "[pipewire] Connect PW context to fd"sv;
        if (fd >= 0) {
          core = pw_context_connect_fd(context, fd, nullptr, 0);
        } else {
          core = pw_context_connect(context, nullptr, 0);
        }
        if (core) {
          pw_core_add_listener(core, &core_listener, &core_events, nullptr);
        } else {
          BOOST_LOG(debug) << "[pipewire] Failed to connect to PW core. Error: "sv << errno << "(" << strerror(errno) << ")"sv;
          return -1;
        }
      } else {
        BOOST_LOG(debug) << "[pipewire] Failed to setup PW context. Error: "sv << errno << "(" << strerror(errno) << ")"sv;
        return -1;
      }

      pw_thread_loop_unlock(loop);
      return 0;
    }

    void cleanup_stream() {
      BOOST_LOG(debug) << "[pipewire] Cleaning up stream"sv;
      if (loop && stream_data.stream) {
        pw_thread_loop_lock(loop);

        // 1. Lock the frame mutex to stop fill_img
        BOOST_LOG(debug) << "[pipewire] Stop fill_img"sv;
        {
          std::scoped_lock lock(stream_data.frame_mutex);
          stream_data.frame_ready = false;
          stream_data.current_buffer = nullptr;
        }

        if (stream_data.stream) {
          BOOST_LOG(debug) << "[pipewire] Disconnect stream"sv;
          pw_stream_disconnect(stream_data.stream);
          BOOST_LOG(debug) << "[pipewire] Destroy stream"sv;
          pw_stream_destroy(stream_data.stream);
          stream_data.stream = nullptr;
        }

        pw_thread_loop_unlock(loop);
      }
    }

    int ensure_stream(const platf::mem_type_e mem_type, const uint32_t width, const uint32_t height, const uint32_t refresh_rate, const struct dmabuf_format_info_t *dmabuf_infos, const int n_dmabuf_infos, const bool display_is_nvidia) {
      pw_thread_loop_lock(loop);
      if (!stream_data.stream) {
        if (!core) {
          BOOST_LOG(debug) << "[pipewire] PW core not available. Cannot ensure stream."sv;
          pw_thread_loop_unlock(loop);
          return -1;
        }

        struct pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr);

        BOOST_LOG(debug) << "[pipewire] Create PW stream"sv;
        stream_data.stream = pw_stream_new(core, "Sunshine Video Capture", props);
        pw_stream_add_listener(stream_data.stream, &stream_data.stream_listener, &stream_events, &stream_data);

        std::array<uint8_t, SPA_POD_BUFFER_SIZE> buffer;
        struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());

        int n_params = 0;
        std::array<const struct spa_pod *, MAX_PARAMS> params;

        // Add preferred parameters for DMA-BUF with modifiers
        // Use DMA-BUF for VAAPI, or for CUDA when the display GPU is NVIDIA (pure NVIDIA system).
        // On hybrid GPU systems (Intel+NVIDIA), DMA-BUFs come from the Intel GPU and cannot
        // be imported into CUDA, so we fall back to memory buffers in that case.
        bool use_dmabuf = n_dmabuf_infos > 0 && (mem_type == platf::mem_type_e::vaapi ||
                                                 mem_type == platf::mem_type_e::vulkan ||
                                                 (mem_type == platf::mem_type_e::cuda && display_is_nvidia));
        if (use_dmabuf) {
          for (int i = 0; i < n_dmabuf_infos; i++) {
            auto format_param = build_format_parameter(&pod_builder, width, height, refresh_rate, dmabuf_infos[i].format, dmabuf_infos[i].modifiers, dmabuf_infos[i].n_modifiers);
            params[n_params] = format_param;
            n_params++;
          }
        }

        // Add fallback for memptr
        for (const auto &fmt : format_map) {
          if (fmt.fourcc == 0) {
            break;
          }
          auto format_param = build_format_parameter(&pod_builder, width, height, refresh_rate, fmt.pw_format, nullptr, 0);
          params[n_params] = format_param;
          n_params++;
        }
        BOOST_LOG(debug) << "[pipewire] Connect PW stream - fd "sv << fd << " node "sv << node;
        pw_stream_connect(stream_data.stream, PW_DIRECTION_INPUT, node, (enum pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params.data(), n_params);
      }
      pw_thread_loop_unlock(loop);
      return 0;
    }

    static void close_img_fds(egl::img_descriptor_t *img_descriptor) {
      for (int &fd : img_descriptor->sd.fds) {
        if (fd >= 0) {
          close(fd);
          fd = -1;
        }
      }
    }

    static void fill_img_metadata(egl::img_descriptor_t *img_descriptor, struct spa_buffer *buf) {
      img_descriptor->frame_timestamp = std::chrono::steady_clock::now();

      struct spa_meta_header *h = static_cast<struct spa_meta_header *>(
        spa_buffer_find_meta_data(buf, SPA_META_Header, sizeof(*h))
      );
      if (h) {
        img_descriptor->seq = h->seq;
        img_descriptor->pts = h->pts;
      }

      if (buf->n_datas > 0) {
        img_descriptor->pw_flags = buf->datas[0].chunk->flags;
      }

      struct spa_meta_region *damage = static_cast<struct spa_meta_region *>(
        spa_buffer_find_meta_data(buf, SPA_META_VideoDamage, sizeof(*damage))
      );
      img_descriptor->pw_damage = (damage && damage->region.size.width > 0 && damage->region.size.height > 0) ? std::optional<bool>(true) : std::nullopt;
    }

    static void fill_img_dmabuf(egl::img_descriptor_t *img_descriptor, struct spa_buffer *buf, const stream_data_t &d) {
      img_descriptor->sd.width = d.format.info.raw.size.width;
      img_descriptor->sd.height = d.format.info.raw.size.height;
      img_descriptor->sd.modifier = d.format.info.raw.modifier;
      img_descriptor->sd.fourcc = d.drm_format;
      for (int i = 0; i < MIN(buf->n_datas, 4); i++) {
        img_descriptor->sd.fds[i] = dup(buf->datas[i].fd);
        img_descriptor->sd.pitches[i] = buf->datas[i].chunk->stride;
        img_descriptor->sd.offsets[i] = buf->datas[i].chunk->offset;
      }
    }

    void fill_img(platf::img_t *img) {
      pw_thread_loop_lock(loop);
      std::scoped_lock lock(stream_data.frame_mutex);

      if (stream_data.shared && stream_data.shared->stream_dead.load()) {
        img->data = nullptr;
        close_img_fds(static_cast<egl::img_descriptor_t *>(img));
        pw_thread_loop_unlock(loop);
        return;
      }

      if (!stream_data.current_buffer) {
        img->data = nullptr;
        pw_thread_loop_unlock(loop);
        return;
      }

      struct spa_buffer *buf = stream_data.current_buffer->buffer;
      if (buf->datas[0].chunk->size != 0) {
        auto *img_descriptor = static_cast<egl::img_descriptor_t *>(img);
        fill_img_metadata(img_descriptor, buf);
        if (buf->datas[0].type == SPA_DATA_DmaBuf) {
          fill_img_dmabuf(img_descriptor, buf, stream_data);
        } else {
          img->data = stream_data.front_buffer->data();
          img->row_pitch = stream_data.local_stride;
        }
      }

      pw_thread_loop_unlock(loop);
    }

    void set_negotiate_maxframerate(bool negotiate_maxframerate) {
      negotiate_maxframerate_ = negotiate_maxframerate;
    }

  private:
    struct pw_thread_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct spa_hook core_listener;
    struct stream_data_t stream_data;
    int fd;
    int node;
    bool negotiate_maxframerate_ = true;

    struct spa_pod *build_format_parameter(struct spa_pod_builder *b, uint32_t width, uint32_t height, uint32_t refresh_rate, int32_t format, uint64_t *modifiers, int n_modifiers) {
      struct spa_pod_frame object_frame;
      struct spa_pod_frame modifier_frame;
      std::array<struct spa_rectangle, 3> sizes;
      std::array<struct spa_fraction, 3> framerates;

      sizes[0] = SPA_RECTANGLE(width, height);  // Preferred
      sizes[1] = SPA_RECTANGLE(1, 1);
      sizes[2] = SPA_RECTANGLE(8192, 4096);

      framerates[0] = SPA_FRACTION(0, 1);  // default; we only want variable rate, thus bypassing compositor pacing
      framerates[1] = SPA_FRACTION(0, 1);  // min
      framerates[2] = SPA_FRACTION(0, 1);  // max

      spa_pod_builder_push_object(b, &object_frame, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
      spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
      spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&sizes[0], &sizes[1], &sizes[2]), 0);
      spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&framerates[0]), 0);
      if (negotiate_maxframerate_) {
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction(&framerates[0], &framerates[1], &framerates[2]), 0);
      }

      if (n_modifiers) {
        spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        spa_pod_builder_push_choice(b, &modifier_frame, SPA_CHOICE_Enum, 0);

        // Preferred value, we pick the first modifier be the preferred one
        spa_pod_builder_long(b, modifiers[0]);
        for (uint32_t i = 0; i < n_modifiers; i++) {
          spa_pod_builder_long(b, modifiers[i]);
        }

        spa_pod_builder_pop(b, &modifier_frame);
      }

      return static_cast<struct spa_pod *>(spa_pod_builder_pop(b, &object_frame));
    }

    static void on_core_info_cb([[maybe_unused]] void *user_data, const struct pw_core_info *pw_info) {
      BOOST_LOG(info) << "[pipewire] Connected to pipewire version "sv << pw_info->version;
    }

    static void on_core_error_cb([[maybe_unused]] void *user_data, const uint32_t id, const int seq, [[maybe_unused]] int res, const char *message) {
      BOOST_LOG(info) << "[pipewire] Pipewire Error, id:"sv << id << " seq:"sv << seq << " message: "sv << message;
    }

    constexpr static const struct pw_core_events core_events = {
      .version = PW_VERSION_CORE_EVENTS,
      .info = on_core_info_cb,
      .error = on_core_error_cb,
    };

    static void on_stream_state_changed(void *user_data, enum pw_stream_state old, enum pw_stream_state state, const char *err_msg) {
      BOOST_LOG(debug) << "[pipewire] PipeWire stream state: " << pw_stream_state_as_string(old)
                       << " -> " << pw_stream_state_as_string(state);

      auto *d = static_cast<stream_data_t *>(user_data);

      switch (state) {
        case PW_STREAM_STATE_PAUSED:
          if (d->shared && old == PW_STREAM_STATE_STREAMING) {
            {
              std::scoped_lock lock(d->frame_mutex);
              d->frame_ready = false;
              d->current_buffer = nullptr;
              d->shared->stream_dead.store(true);
              d->shared->current_state = state;
              d->shared->previous_state = old;
              d->shared->err_msg = "";
            }
            d->frame_cv.notify_all();
          }
          break;
        case PW_STREAM_STATE_ERROR:
          {
            std::scoped_lock lock(d->frame_mutex);
            d->shared->current_state = state;
            d->shared->previous_state = old;
            d->shared->err_msg = std::string(err_msg);
          }
          [[fallthrough]];
        case PW_STREAM_STATE_UNCONNECTED:
          if (d->shared) {
            d->shared->stream_dead.store(true);
            d->frame_cv.notify_all();
          }
          break;
        default:
          break;
      }
    }

    static void on_process(void *user_data) {
      const auto d = static_cast<struct stream_data_t *>(user_data);
      struct pw_buffer *b = nullptr;

      // 1. Drain the queue: Always grab the most recent buffer
      while (struct pw_buffer *aux = pw_stream_dequeue_buffer(d->stream)) {
        if (b) {
          pw_stream_queue_buffer(d->stream, b);  // Return the older, unused buffer
        }
        b = aux;
      }

      if (!b) {
        return;
      }

      // 2. Fast Path: DMA-BUF
      if (b->buffer->datas[0].type == SPA_DATA_DmaBuf) {
        std::scoped_lock lock(d->frame_mutex);
        if (d->current_buffer) {
          pw_stream_queue_buffer(d->stream, d->current_buffer);
        }
        d->current_buffer = b;
        d->frame_ready = true;
      }
      // 3. Optimized Path: Software/MemPtr
      else if (b->buffer->datas[0].data != nullptr) {
        size_t size = b->buffer->datas[0].chunk->size;

        // Perform the copy to the BACK buffer while NOT holding the lock
        if (d->back_buffer->size() < size) {
          d->back_buffer->resize(size);
        }
        std::memcpy(d->back_buffer->data(), b->buffer->datas[0].data, size);

        {
          // Lock only for the pointer swap and state update
          std::scoped_lock lock(d->frame_mutex);
          std::swap(d->front_buffer, d->back_buffer);

          d->local_stride = b->buffer->datas[0].chunk->stride;
          d->frame_ready = true;
          d->current_buffer = b;
        }

        // Release the PW buffer immediately after copy
        pw_stream_queue_buffer(d->stream, b);
      }

      d->frame_cv.notify_one();
    }

    static void on_param_changed(void *user_data, uint32_t id, const struct spa_pod *param) {
      const auto d = static_cast<struct stream_data_t *>(user_data);

      d->current_buffer = nullptr;

      if (param == nullptr || id != SPA_PARAM_Format) {
        return;
      }
      if (spa_format_parse(param, &d->format.media_type, &d->format.media_subtype) < 0) {
        return;
      }
      if (d->format.media_type != SPA_MEDIA_TYPE_video || d->format.media_subtype != SPA_MEDIA_SUBTYPE_raw) {
        return;
      }
      if (spa_format_video_raw_parse(param, &d->format.info.raw) < 0) {
        return;
      }

      BOOST_LOG(info) << "[pipewire] Video format: "sv << d->format.info.raw.format;
      BOOST_LOG(info) << "[pipewire] Size: "sv << d->format.info.raw.size.width << "x"sv << d->format.info.raw.size.height;
      if (d->format.info.raw.max_framerate.num == 0 && d->format.info.raw.max_framerate.denom == 1) {
        BOOST_LOG(info) << "[pipewire] Framerate (from compositor): 0/1 (variable rate capture)";
      } else {
        BOOST_LOG(info) << "[pipewire] Framerate (from compositor): "sv << d->format.info.raw.framerate.num << "/"sv << d->format.info.raw.framerate.denom;
        BOOST_LOG(info) << "[pipewire] Framerate (from compositor, max): "sv << d->format.info.raw.max_framerate.num << "/"sv << d->format.info.raw.max_framerate.denom;
      }

      int physical_w = d->format.info.raw.size.width;
      int physical_h = d->format.info.raw.size.height;

      if (d->shared) {
        int old_w = d->shared->negotiated_width.load();
        int old_h = d->shared->negotiated_height.load();

        if (physical_w != old_w || physical_h != old_h) {
          d->shared->negotiated_width.store(physical_w);
          d->shared->negotiated_height.store(physical_h);
        }
      }

      uint64_t drm_format = 0;
      for (const auto &fmt : format_map) {
        if (fmt.fourcc == 0) {
          break;
        }
        if (fmt.pw_format == d->format.info.raw.format) {
          drm_format = fmt.fourcc;
        }
      }
      d->drm_format = drm_format;

      uint32_t buffer_types = 0;
      if (spa_pod_find_prop(param, nullptr, SPA_FORMAT_VIDEO_modifier) != nullptr && d->drm_format) {
        BOOST_LOG(info) << "[pipewire] using DMA-BUF buffers"sv;
        buffer_types |= 1 << SPA_DATA_DmaBuf;
      } else {
        BOOST_LOG(info) << "[pipewire] using memory buffers"sv;
        buffer_types |= 1 << SPA_DATA_MemPtr;
      }

      // Ack the buffer type and metadata
      std::array<uint8_t, SPA_POD_BUFFER_SIZE> buffer;
      std::array<const struct spa_pod *, 3> params;
      int n_params = 0;
      struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());
      auto buffer_param = static_cast<const struct spa_pod *>(spa_pod_builder_add_object(&pod_builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_dataType, SPA_POD_Int(buffer_types)));
      params[n_params] = buffer_param;
      n_params++;
      auto meta_param = static_cast<const struct spa_pod *>(spa_pod_builder_add_object(&pod_builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header), SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_header))));
      params[n_params] = meta_param;
      n_params++;
      int videoDamageRegionCount = 16;
      auto damage_param = static_cast<const struct spa_pod *>(spa_pod_builder_add_object(&pod_builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoDamage), SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int(sizeof(struct spa_meta_region) * videoDamageRegionCount, sizeof(struct spa_meta_region) * 1, sizeof(struct spa_meta_region) * videoDamageRegionCount)));
      params[n_params] = damage_param;
      n_params++;

      pw_stream_update_params(d->stream, params.data(), n_params);
    }

    constexpr static const struct pw_stream_events stream_events = {
      .version = PW_VERSION_STREAM_EVENTS,
      .state_changed = on_stream_state_changed,
      .param_changed = on_param_changed,
      .process = on_process,
    };
  };

  class pipewire_display_t: public platf::display_t {
  public:
    static bool init_pipewire_and_check_hwdevice_type(platf::mem_type_e hwdevice_type) {
      // Initialize pipewire to load necessary modules
      pw_init(nullptr, nullptr);

      // Check if we have a matching hwdevice_type
      switch (hwdevice_type) {
        using enum platf::mem_type_e;
        case system:
        case vaapi:
        case cuda:
        case vulkan:
          return true;
        default:
          return false;
      }
    }

    /**
     *  @brief Configure the pipewire stream
     *  @param display_name provide a stream for this display_name
     *  @param out_pipewire_fd set to the pipewire fd for the stream in during function call (or -1 for using the local context)
     *  @param out_pipewire_node set to the pipewire node of the stream in during function call
     *  @returns 0 if the stream successfully configured
     */
    virtual int configure_stream(const std::string &display_name, int &out_pipewire_fd, int &out_pipewire_node) = 0;

    /**
     *  @brief Verify and update display parameters for logical dimensions, desktop dimensions and logical desktop dimensions (default is adapted from wlgrab)
     */
    virtual void verify_and_update_display_parameters() {
      // Set environment dimensions to stream dimensions (unless we find something better to report here)
      if (env_height <= 0 || env_width <= 0) {
        this->env_width = width;
        this->env_height = height;
        BOOST_LOG(debug) << "[pipewire] Desktop Resolution: "sv << env_width << 'x' << env_height;
      }
      // Query logical sizes directly using wayland wl::monitors() and match current screen based on offset/dimensions
      if (logical_height <= 0 || logical_width <= 0 || env_logical_height <= 0 || env_logical_width <= 0) {
        int desktop_logical_width = 0;
        int desktop_logical_height = 0;
        for (const auto &monitor : wl::monitors()) {
          // If logical_width and logical_height are not valid try to update them to correct values by matching to monitor position/dimension or position/logical dimensions
          // since we're iterating for maximum environment size anyway
          if ((logical_width <= 0 || logical_height <= 0) && monitor->viewport.offset_x == offset_x && monitor->viewport.offset_y == offset_y && ((monitor->viewport.width == width && monitor->viewport.height == height) || (monitor->viewport.logical_width == width && monitor->viewport.logical_height == height))) {
            this->logical_width = monitor->viewport.logical_width;
            this->logical_height = monitor->viewport.logical_height;
            BOOST_LOG(debug) << "[pipewire] Logical Resolution: "sv << logical_width << 'x' << logical_height;
          }
          // Update logical dimensions to setup maximum environment size over all screens
          desktop_logical_width = std::max(desktop_logical_width, monitor->viewport.offset_x + monitor->viewport.logical_width);
          desktop_logical_height = std::max(desktop_logical_height, monitor->viewport.offset_y + monitor->viewport.logical_height);
        }
        this->env_logical_width = std::max(env_logical_width, desktop_logical_width);
        this->env_logical_height = std::max(env_logical_height, desktop_logical_height);
        BOOST_LOG(debug) << "[pipewire] Logical Desktop Resolution: "sv << env_logical_width << 'x' << env_logical_height;
      }
    }

    int init(platf::mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config) {
      // calculate frame interval we should capture at
      framerate = config.framerate;
      if (config.framerateX100 > 0) {
        AVRational fps_strict = ::video::framerateX100_to_rational(config.framerateX100);
        delay = std::chrono::nanoseconds(
          (static_cast<int64_t>(fps_strict.den) * 1'000'000'000LL) / fps_strict.num
        );
        BOOST_LOG(info) << "[pipewire] Requested frame rate [" << fps_strict.num << "/" << fps_strict.den << ", approx. " << av_q2d(fps_strict) << " fps]";
      } else {
        delay = std::chrono::nanoseconds {1s} / framerate;
        BOOST_LOG(info) << "[pipewire] Requested frame rate [" << framerate << "fps]";
      }
      mem_type = hwdevice_type;

      if (get_dmabuf_modifiers() < 0) {
        return -1;
      }

      int pipewire_fd = -1;
      int pipewire_node = -1;
      // Fetch stream info
      if (configure_stream(display_name, pipewire_fd, pipewire_node) < 0 || pipewire_node < 0) {
        BOOST_LOG(error) << "[pipewire] Could not find display with name: '"sv << display_name << "'";
        return -1;
      }
      BOOST_LOG(info) << "[pipewire] Streaming display '"sv << display_name << "' from position: "sv << offset_x << "x"sv << offset_y << " resolution: "sv << width << "x"sv << height;

      // Verify or update display parameters for streaming to ensure absolute touch inputs work as expected
      verify_and_update_display_parameters();

      framerate = config.framerate;

      if (!shared_state) {
        shared_state = std::make_shared<shared_state_t>();
      } else {
        shared_state->stream_dead.store(false);
        shared_state->negotiated_width.store(0);
        shared_state->negotiated_height.store(0);
      }

      if (pipewire.init(pipewire_fd, pipewire_node, shared_state) < 0) {
        BOOST_LOG(error) << "[pipewire] Failed to init pipewire. pipewire_t::init() failed.";
        return -1;
      }

      // Start PipeWire now so format negotiation can proceed before capture start
      if (pipewire.ensure_stream(mem_type, width, height, framerate, dmabuf_infos.data(), n_dmabuf_infos, display_is_nvidia) < 0) {
        BOOST_LOG(error) << "[pipewire] Failed to ensure pipewire stream. pipewire_t::init() failed.";
        return -1;
      }

      // Wait for pipewire negotiation to finish so we have the proper negotiated dimensions
      int timeout_ms = 1500;
      int negotiated_w = 0;
      int negotiated_h = 0;
      while (timeout_ms > 0) {
        negotiated_w = shared_state->negotiated_width.load();
        negotiated_h = shared_state->negotiated_height.load();
        if (negotiated_w > 0 && negotiated_h > 0) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        timeout_ms -= 10;
      }
      // Set width and height to the values negotiated by pipewire
      if (negotiated_w > 0 && negotiated_h > 0 && (negotiated_w != width || negotiated_h != height)) {
        width = negotiated_w;
        height = negotiated_h;
        BOOST_LOG(info) << "[pipewire] Using negotiated Resolution: "sv << width << "x" << height;

        // Reset and update display parameters for negotiated resolution
        env_width = 0;
        env_height = 0;
        logical_height = 0;
        logical_width = 0;
        env_logical_height = 0;
        env_logical_width = 0;
        verify_and_update_display_parameters();
      }

      return 0;
    }

    platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool show_cursor) {
      // FIXME: show_cursor is ignored
      auto deadline = std::chrono::steady_clock::now() + timeout;
      int retries = 0;

      while (std::chrono::steady_clock::now() < deadline) {
        if (!wait_for_frame(deadline)) {
          return platf::capture_e::timeout;
        }

        if (!pull_free_image_cb(img_out)) {
          return platf::capture_e::interrupted;
        }

        auto *img_egl = static_cast<egl::img_descriptor_t *>(img_out.get());
        img_egl->reset();
        pipewire.fill_img(img_egl);

        // Check if we got valid data (either DMA-BUF fd or memory pointer), then filter duplicates
        if ((img_egl->sd.fds[0] >= 0 || img_egl->data != nullptr) && !is_buffer_redundant(img_egl)) {
          // Update frame metadata
          update_metadata(img_egl, retries);
          return platf::capture_e::ok;
        }

        // No valid frame yet, or it was a duplicate
        retries++;
      }
      return platf::capture_e::timeout;
    }

    std::shared_ptr<platf::img_t> alloc_img() override {
      // Note: this img_t type is also used for memory buffers
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

    virtual bool check_stream_dead(platf::capture_e &out_status) {
      return false;  // Return to default stream dead handling.
    }

    platf::capture_e capture(const push_captured_image_cb_t &push_captured_image_cb, const pull_free_image_cb_t &pull_free_image_cb, bool *cursor) override {
      auto next_frame = std::chrono::steady_clock::now();

      if (pipewire.ensure_stream(mem_type, width, height, framerate, dmabuf_infos.data(), n_dmabuf_infos, display_is_nvidia) < 0) {
        BOOST_LOG(error) << "[pipewire] Failed to ensure pipewire stream. capture() failed with error.";
        return platf::capture_e::error;
      }
      sleep_overshoot_logger.reset();

      while (true) {
        // Check if PipeWire signaled a dead stream
        if (shared_state->stream_dead.exchange(false)) {
          // Additional custom error-handling for subclasses on stream dead event
          if (platf::capture_e status; check_stream_dead(status)) {
            return status;
          }
          // Re-init the capture if the stream is dead for any other reason
          BOOST_LOG(warning) << "[pipewire] PipeWire stream disconnected. Forcing session reset."sv;
          return platf::capture_e::reinit;
        }

        // Advance to (or catch up with) next delay interval
        auto now = std::chrono::steady_clock::now();
        while (next_frame < now) {
          next_frame += delay;
        }

        if (next_frame > now) {
          std::this_thread::sleep_until(next_frame);
          sleep_overshoot_logger.first_point(next_frame);
          sleep_overshoot_logger.second_point_now_and_log();
        }

        std::shared_ptr<platf::img_t> img_out;
        switch (const auto status = snapshot(pull_free_image_cb, img_out, 1000ms, *cursor)) {
          case platf::capture_e::reinit:
          case platf::capture_e::error:
          case platf::capture_e::interrupted:
            pipewire.frame_cv().notify_all();
            return status;
          case platf::capture_e::timeout:
            if (!pull_free_image_cb(img_out)) {
              // Detect if shutdown is pending
              BOOST_LOG(debug) << "[pipewire] PipeWire: timeout -> interrupt nudge";
              pipewire.frame_cv().notify_all();
              return platf::capture_e::interrupted;
            }
            if (!push_captured_image_cb(std::move(img_out), false)) {
              BOOST_LOG(debug) << "[pipewire] PipeWire: !push_captured_image_cb -> ok";
              return platf::capture_e::ok;
            }
            break;
          case platf::capture_e::ok:
            if (!push_captured_image_cb(std::move(img_out), true)) {
              BOOST_LOG(debug) << "[pipewire] PipeWire: !push_captured_image_cb -> ok";
              return platf::capture_e::ok;
            }
            break;
          default:
            BOOST_LOG(error) << "[pipewire] Unrecognized capture status ["sv << std::to_underlying(status) << ']';
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

#ifdef SUNSHINE_BUILD_VULKAN
      if (mem_type == platf::mem_type_e::vulkan && n_dmabuf_infos > 0) {
        return vk::make_avcodec_encode_device_vram(width, height, 0, 0);
      }
#endif

#ifdef SUNSHINE_BUILD_CUDA
      if (mem_type == platf::mem_type_e::cuda) {
        if (display_is_nvidia && n_dmabuf_infos > 0) {
          // Display GPU is NVIDIA - can use DMA-BUF directly
          return cuda::make_avcodec_gl_encode_device(width, height, 0, 0);
        } else {
          // Hybrid system (Intel display + NVIDIA encode) - use memory buffer path
          // DMA-BUFs from Intel GPU cannot be imported into CUDA
          return cuda::make_avcodec_encode_device(width, height, false);
        }
      }
#endif

      return std::make_unique<platf::avcodec_encode_device_t>();
    }

    int dummy_img(platf::img_t *img) override {
      if (!img) {
        return -1;
      }

      img->data = new std::uint8_t[img->height * img->row_pitch];
      std::fill_n(img->data, img->height * img->row_pitch, 0);
      return 0;
    }

  private:
    bool is_buffer_redundant(const egl::img_descriptor_t *img) {
      // Check for corrupted frame
      if (img->pw_flags.has_value() && (img->pw_flags.value() & SPA_CHUNK_FLAG_CORRUPTED)) {
        return true;
      }

      // If PTS is identical, only drop if damage metadata confirms no change
      if (img->pts.has_value() && last_pts.has_value() && img->pts.value() == last_pts.value()) {
        return img->pw_damage.has_value() && !img->pw_damage.value();
      }

      return false;
    }

    void update_metadata(egl::img_descriptor_t *img, int retries) {
      last_seq = img->seq;
      last_pts = img->pts;
      img->sequence = ++sequence;

      if (retries > 0) {
        BOOST_LOG(debug) << "[pipewire] Processed frame after " << retries << " redundant events."sv;
      }
    }

    bool wait_for_frame(std::chrono::steady_clock::time_point deadline) {
      std::unique_lock<std::mutex> lock(pipewire.frame_mutex());

      bool success = pipewire.frame_cv().wait_until(lock, deadline, [&] {
        return pipewire.is_frame_ready() || shared_state->stream_dead.load();
      });

      if (success) {
        pipewire.set_frame_ready(false);
        return true;
      }
      return false;
    }

    static uint32_t lookup_pw_format(uint64_t fourcc) {
      for (const auto &fmt : format_map) {
        if (fmt.fourcc == 0) {
          break;
        }
        if (fmt.fourcc == fourcc) {
          return fmt.pw_format;
        }
      }
      return 0;
    }

    void query_dmabuf_formats(EGLDisplay egl_display) {
      EGLint num_dmabuf_formats = 0;
      std::array<EGLint, MAX_DMABUF_FORMATS> dmabuf_formats = {0};
      eglQueryDmaBufFormatsEXT(egl_display, MAX_DMABUF_FORMATS, dmabuf_formats.data(), &num_dmabuf_formats);

      if (num_dmabuf_formats > MAX_DMABUF_FORMATS) {
        BOOST_LOG(warning) << "[pipewire] Some DMA-BUF formats are being ignored"sv;
      }

      for (EGLint i = 0; i < MIN(num_dmabuf_formats, MAX_DMABUF_FORMATS); i++) {
        uint32_t pw_format = lookup_pw_format(dmabuf_formats[i]);
        if (pw_format == 0) {
          continue;
        }

        EGLint num_modifiers = 0;
        std::array<EGLuint64KHR, MAX_DMABUF_MODIFIERS> mods = {0};
        eglQueryDmaBufModifiersEXT(egl_display, dmabuf_formats[i], MAX_DMABUF_MODIFIERS, mods.data(), nullptr, &num_modifiers);

        if (num_modifiers > MAX_DMABUF_MODIFIERS) {
          BOOST_LOG(warning) << "[pipewire] Some DMA-BUF modifiers are being ignored"sv;
        }

        dmabuf_infos[n_dmabuf_infos].format = pw_format;
        dmabuf_infos[n_dmabuf_infos].n_modifiers = MIN(num_modifiers, MAX_DMABUF_MODIFIERS);
        dmabuf_infos[n_dmabuf_infos].modifiers =
          static_cast<uint64_t *>(g_memdup2(mods.data(), sizeof(uint64_t) * dmabuf_infos[n_dmabuf_infos].n_modifiers));
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

      // Detect if this is a pure NVIDIA system (not hybrid Intel+NVIDIA)
      // On hybrid systems, the wayland compositor typically runs on Intel,
      // so DMA-BUFs from portal will come from Intel and cannot be imported into CUDA.
      // Check if Intel GPU exists - if so, assume hybrid system and disable CUDA DMA-BUF.
      bool has_intel_gpu = std::ifstream("/sys/class/drm/card0/device/vendor").good() ||
                           std::ifstream("/sys/class/drm/card1/device/vendor").good();
      if (has_intel_gpu) {
        // Read vendor IDs to check for Intel (0x8086)
        auto check_intel = [](const std::string &path) {
          if (std::ifstream f(path); f.good()) {
            std::string vendor;
            f >> vendor;
            return vendor == "0x8086";
          }
          return false;
        };
        bool intel_present = check_intel("/sys/class/drm/card0/device/vendor") ||
                             check_intel("/sys/class/drm/card1/device/vendor");
        if (intel_present) {
          BOOST_LOG(info) << "[pipewire] Hybrid GPU system detected (Intel + discrete) - CUDA will use memory buffers"sv;
          display_is_nvidia = false;
        } else {
          // No Intel GPU found, check if NVIDIA is present
          const char *vendor = eglQueryString(egl_display.get(), EGL_VENDOR);
          if (vendor && std::string_view(vendor).contains("NVIDIA")) {
            BOOST_LOG(info) << "[pipewire] Pure NVIDIA system - DMA-BUF will be enabled for CUDA"sv;
            display_is_nvidia = true;
          }
        }
      }

      if (eglQueryDmaBufFormatsEXT && eglQueryDmaBufModifiersEXT) {
        query_dmabuf_formats(egl_display.get());
      }

      return 0;
    }

    platf::mem_type_e mem_type;
    wl::display_t wl_display;
    std::array<struct dmabuf_format_info_t, MAX_DMABUF_FORMATS> dmabuf_infos;
    int n_dmabuf_infos;
    bool display_is_nvidia = false;  // Track if display GPU is NVIDIA
    std::chrono::nanoseconds delay;
    std::optional<std::uint64_t> last_pts {};
    std::optional<std::uint64_t> last_seq {};
    std::uint64_t sequence {};
    uint32_t framerate;

  protected:
    // Allow subclasses to access for pipewire requirements setup and stream dead checks
    pipewire_t pipewire;
    std::shared_ptr<shared_state_t> shared_state;
  };
}  // namespace pipewire
