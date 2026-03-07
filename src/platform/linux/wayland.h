/**
 * @file src/platform/linux/wayland.h
 * @brief Declarations for Wayland capture.
 */
#pragma once

// standard includes
#include <bitset>

#ifdef SUNSHINE_BUILD_WAYLAND
  #include <ext-image-capture-source-v1.h>
  #include <ext-image-copy-capture-v1.h>
  #include <linux-dmabuf-v1.h>
  #include <wlr-screencopy-unstable-v1.h>
  #include <xdg-output-unstable-v1.h>
#endif

// local includes
#include "graphics.h"

/**
 * The classes defined in this macro block should only be used by
 * cpp files whose compilation depends on SUNSHINE_BUILD_WAYLAND
 */
#ifdef SUNSHINE_BUILD_WAYLAND

namespace wl {
  using display_internal_t = util::safe_ptr<wl_display, wl_display_disconnect>;

  class frame_t {
  public:
    frame_t();
    void destroy();

    egl::surface_descriptor_t sd;
  };

  class dmabuf_t {
  public:
    enum status_e {
      WAITING,  ///< Waiting for a frame
      READY,  ///< Frame is ready
      REINIT,  ///< Reinitialize the frame
    };

    dmabuf_t();
    ~dmabuf_t();

    dmabuf_t(dmabuf_t &&) = delete;
    dmabuf_t(const dmabuf_t &) = delete;
    dmabuf_t &operator=(const dmabuf_t &) = delete;
    dmabuf_t &operator=(dmabuf_t &&) = delete;

    // wlr-screencopy based capture
    void screencopy_create(zwlr_screencopy_manager_v1 *screencopy_manager, zwp_linux_dmabuf_v1 *dmabuf_interface, wl_output *output);
    void screencopy_capture(bool blend_cursor = false);
    void buffer_params_created(zwp_linux_buffer_params_v1 *params, struct wl_buffer *wl_buffer);
    void buffer_params_failed(zwp_linux_buffer_params_v1 *params);
    void buffer_feedback_done(zwp_linux_dmabuf_feedback_v1 *feedback);
    void buffer_feedback_format_table(zwp_linux_dmabuf_feedback_v1 *feedback, int fd, int size);
    void buffer_feedback_main_device(zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *device);
    void buffer_feedback_tranche_done(zwp_linux_dmabuf_feedback_v1 *feedback);
    void buffer_feedback_tranche_target_device(zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *device);
    void buffer_feedback_tranche_formats(zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *formats);
    void buffer_feedback_tranche_flags(zwp_linux_dmabuf_feedback_v1 *feedback, int flags);
    void buffer(zwlr_screencopy_frame_v1 *frame, std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t stride);
    void linux_dmabuf(zwlr_screencopy_frame_v1 *frame, std::uint32_t format, std::uint32_t width, std::uint32_t height);
    void buffer_done(zwlr_screencopy_frame_v1 *frame);
    void flags(zwlr_screencopy_frame_v1 *frame, std::uint32_t flags);
    void damage(zwlr_screencopy_frame_v1 *frame, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height);
    void ready(zwlr_screencopy_frame_v1 *frame, std::uint32_t tv_sec_hi, std::uint32_t tv_sec_lo, std::uint32_t tv_nsec);
    void failed(zwlr_screencopy_frame_v1 *frame);

    // ext-image-copy-capture based capture
    void icc_create(ext_image_copy_capture_manager_v1 *manager, ext_output_image_capture_source_manager_v1 *source_manager, zwp_linux_dmabuf_v1 *dmabuf_interface, wl_output *output, bool blend_cursor = false);
    void icc_capture(bool blend_cursor = false);
    void icc_cleanup();
    void icc_buffer_params_created(zwp_linux_buffer_params_v1 *params, struct wl_buffer *buffer);
    void icc_buffer_params_failed(zwp_linux_buffer_params_v1 *params);
    void icc_session_buffer_size(ext_image_copy_capture_session_v1 *session, std::uint32_t width, std::uint32_t height);
    void icc_session_shm_format(ext_image_copy_capture_session_v1 *session, std::uint32_t format);
    void icc_session_dmabuf_device(ext_image_copy_capture_session_v1 *session, struct wl_array *device);
    void icc_session_dmabuf_format(ext_image_copy_capture_session_v1 *session, std::uint32_t format, struct wl_array *modifiers);
    void icc_session_done(ext_image_copy_capture_session_v1 *session);
    void icc_session_stopped(ext_image_copy_capture_session_v1 *session);
    void icc_frame_transform(ext_image_copy_capture_frame_v1 *frame, std::uint32_t transform);
    void icc_frame_damage(ext_image_copy_capture_frame_v1 *frame, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
    void icc_frame_presentation_time(ext_image_copy_capture_frame_v1 *frame, std::uint32_t tv_sec_hi, std::uint32_t tv_sec_lo, std::uint32_t tv_nsec);
    void icc_frame_ready(ext_image_copy_capture_frame_v1 *frame);
    void icc_frame_failed(ext_image_copy_capture_frame_v1 *frame, std::uint32_t reason);

    frame_t *get_next_frame() {
      return current_frame == &frames[0] ? &frames[1] : &frames[0];
    }

    status_e status;
    std::array<frame_t, 2> frames;
    frame_t *current_frame;
    zwlr_screencopy_frame_v1_listener listener;

  private:
    bool init_gbm();
    void cleanup_gbm();
    void create_and_copy_dmabuf(zwlr_screencopy_frame_v1 *frame);
    void icc_create_and_copy_dmabuf(ext_image_copy_capture_frame_v1 *frame);

    zwp_linux_dmabuf_v1 *dmabuf_interface {nullptr};

    struct {
      bool supported {false};
      std::uint32_t format;
      std::uint32_t width;
      std::uint32_t height;
      std::uint32_t stride;
    } shm_info;

    struct {
      bool supported {false};
      std::uint32_t format;
      std::uint32_t width;
      std::uint32_t height;
    } dmabuf_info;

    struct {
      zwlr_screencopy_manager_v1 *manager {nullptr};
      zwlr_screencopy_frame_v1 *frame {nullptr};
      wl_output *output {nullptr};
      bool done {false};
    } screencopy_session;

    struct {
      ext_image_copy_capture_session_v1 *session {nullptr};
      ext_image_copy_capture_frame_v1 *frame {nullptr};
      std::uint32_t width {0};
      std::uint32_t height {0};
      std::uint32_t format {0};
      std::uint64_t modifier {0};
      std::uint64_t num_modifiers {0};
      std::uint64_t *modifiers;
      bool dmabuf_supported {false};
      bool cursor {false};
      bool done {false};
      std::uint32_t fail_count;
    } icc_session;

    struct gbm_device *gbm_device {nullptr};
    struct gbm_bo *current_bo {nullptr};
    struct wl_buffer *current_wl_buffer {nullptr};
    bool y_invert {false};
  };

  class monitor_t {
  public:
    explicit monitor_t(wl_output *output);

    monitor_t(monitor_t &&) = delete;
    monitor_t(const monitor_t &) = delete;
    monitor_t &operator=(const monitor_t &) = delete;
    monitor_t &operator=(monitor_t &&) = delete;

    void listen(zxdg_output_manager_v1 *output_manager);
    void xdg_name(zxdg_output_v1 *, const char *name);
    void xdg_description(zxdg_output_v1 *, const char *description);
    void xdg_position(zxdg_output_v1 *, std::int32_t x, std::int32_t y);
    void xdg_size(zxdg_output_v1 *, std::int32_t width, std::int32_t height);

    void xdg_done(zxdg_output_v1 *) {}

    void wl_geometry(wl_output *wl_output, std::int32_t x, std::int32_t y, std::int32_t physical_width, std::int32_t physical_height, std::int32_t subpixel, const char *make, const char *model, std::int32_t transform) {}

    void wl_mode(wl_output *wl_output, std::uint32_t flags, std::int32_t width, std::int32_t height, std::int32_t refresh);

    void wl_done(wl_output *wl_output) {}

    void wl_scale(wl_output *wl_output, std::int32_t factor) {}

    wl_output *output;
    std::string name;
    std::string description;
    platf::touch_port_t viewport;
    wl_output_listener wl_listener;
    zxdg_output_v1_listener xdg_listener;
  };

  class interface_t {
    struct bind_t {
      std::uint32_t id;
      std::uint32_t version;
    };

  public:
    enum interface_e {
      XDG_OUTPUT,  ///< xdg-output
      WLR_SCREENCOPY,  ///< wlr-screencopy manager
      LINUX_DMABUF,  ///< linux-dmabuf protocol
      EXT_IMAGE_COPY_CAPTURE,  ///< ext-image-copy-capture manager
      EXT_IMAGE_CAPTURE_SOURCE,  ///< ext-image-capture-source manager
      MAX_INTERFACES,  ///< Maximum number of interfaces
    };

    interface_t() noexcept;

    interface_t(interface_t &&) = delete;
    interface_t(const interface_t &) = delete;
    interface_t &operator=(const interface_t &) = delete;
    interface_t &operator=(interface_t &&) = delete;

    void listen(wl_registry *registry);

    bool operator[](interface_e bit) const {
      return interface[bit];
    }

    std::vector<std::unique_ptr<monitor_t>> monitors;
    zwlr_screencopy_manager_v1 *screencopy_manager {nullptr};
    zwp_linux_dmabuf_v1 *dmabuf_interface {nullptr};
    zxdg_output_manager_v1 *output_manager {nullptr};
    ext_image_copy_capture_manager_v1 *icc_manager {nullptr};
    ext_output_image_capture_source_manager_v1 *icc_source_manager {nullptr};

  private:
    void add_interface(wl_registry *registry, std::uint32_t id, const char *interface, std::uint32_t version);
    void del_interface(wl_registry *registry, uint32_t id);

    std::bitset<MAX_INTERFACES> interface;
    wl_registry_listener listener;
  };

  class display_t {
  public:
    /**
     * @brief Initialize display.
     * If display_name == nullptr -> display_name = std::getenv("WAYLAND_DISPLAY")
     * @param display_name The name of the display.
     * @return 0 on success, -1 on failure.
     */
    int init(const char *display_name = nullptr);

    // Roundtrip with Wayland connection
    void roundtrip();

    // Wait up to the timeout to read and dispatch new events
    bool dispatch(std::chrono::milliseconds timeout);

    // Get the registry associated with the display
    // No need to manually free the registry
    wl_registry *registry();

    inline display_internal_t::pointer get() {
      return display_internal.get();
    }

  private:
    display_internal_t display_internal;
  };

  std::vector<std::unique_ptr<monitor_t>> monitors(const char *display_name = nullptr);
  int init();
}  // namespace wl
#else

struct wl_output;
struct zxdg_output_manager_v1;

namespace wl {
  class monitor_t {
  public:
    monitor_t(wl_output *output);

    monitor_t(monitor_t &&) = delete;
    monitor_t(const monitor_t &) = delete;
    monitor_t &operator=(const monitor_t &) = delete;
    monitor_t &operator=(monitor_t &&) = delete;

    void listen(zxdg_output_manager_v1 *output_manager);

    wl_output *output;
    std::string name;
    std::string description;
    platf::touch_port_t viewport;
  };

  inline std::vector<std::unique_ptr<monitor_t>> monitors(const char *display_name = nullptr) {
    return {};
  }

  inline int init() {
    return -1;
  }
}  // namespace wl
#endif
