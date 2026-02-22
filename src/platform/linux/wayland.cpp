/**
 * @file src/platform/linux/wayland.cpp
 * @brief Definitions for Wayland capture.
 */
// standard includes
#include <cstdlib>

// platform includes
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-util.h>
#include <xf86drm.h>

// local includes
#include "graphics.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/round_robin.h"
#include "src/utility.h"
#include "wayland.h"

extern const wl_interface wl_output_interface;

using namespace std::literals;

// Disable warning for converting incompatible functions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wpmf-conversions"

namespace wl {

  // Helper to call C++ method from wayland C callback
  template<class T, class Method, Method m, class... Params>
  static auto classCall(void *data, Params... params) -> decltype(((*reinterpret_cast<T *>(data)).*m)(params...)) {
    return ((*reinterpret_cast<T *>(data)).*m)(params...);
  }

#define CLASS_CALL(c, m) classCall<c, decltype(&c::m), &c::m>

  // Define buffer params listener
  static const struct zwp_linux_buffer_params_v1_listener params_listener = {
    .created = CLASS_CALL(dmabuf_t, buffer_params_created),
    .failed = CLASS_CALL(dmabuf_t, buffer_params_failed),
  };

  // Define buffer feedback listener
  static const struct zwp_linux_dmabuf_feedback_v1_listener feedback_listener = {
    .done = CLASS_CALL(dmabuf_t, buffer_feedback_done),
    .format_table = CLASS_CALL(dmabuf_t, buffer_feedback_format_table),
    .main_device = CLASS_CALL(dmabuf_t, buffer_feedback_main_device),
    .tranche_done = CLASS_CALL(dmabuf_t, buffer_feedback_tranche_done),
    .tranche_target_device = CLASS_CALL(dmabuf_t, buffer_feedback_tranche_target_device),
    .tranche_formats = CLASS_CALL(dmabuf_t, buffer_feedback_tranche_formats),
    .tranche_flags = CLASS_CALL(dmabuf_t, buffer_feedback_tranche_flags),
  };

  int display_t::init(const char *display_name) {
    if (!display_name) {
      display_name = std::getenv("WAYLAND_DISPLAY");
    }

    if (!display_name) {
      BOOST_LOG(error) << "Environment variable WAYLAND_DISPLAY has not been defined"sv;
      return -1;
    }

    display_internal.reset(wl_display_connect(display_name));
    if (!display_internal) {
      BOOST_LOG(error) << "Couldn't connect to Wayland display: "sv << display_name;
      return -1;
    }

    BOOST_LOG(info) << "Found display ["sv << display_name << ']';

    return 0;
  }

  void display_t::roundtrip() {
    wl_display_roundtrip(display_internal.get());
  }

  /**
   * @brief Waits up to the specified timeout to dispatch new events on the wl_display.
   * @param timeout The timeout in milliseconds.
   * @return `true` if new events were dispatched or `false` if the timeout expired.
   */
  bool display_t::dispatch(std::chrono::milliseconds timeout) {
    // Check if any events are queued already. If not, flush
    // outgoing events, and prepare to wait for readability.
    if (wl_display_prepare_read(display_internal.get()) == 0) {
      wl_display_flush(display_internal.get());

      // Wait for an event to come in
      struct pollfd pfd = {};
      pfd.fd = wl_display_get_fd(display_internal.get());
      pfd.events = POLLIN;
      if (poll(&pfd, 1, timeout.count()) == 1 && (pfd.revents & POLLIN)) {
        // Read the new event(s)
        wl_display_read_events(display_internal.get());
      } else {
        // We timed out, so unlock the queue now
        wl_display_cancel_read(display_internal.get());
        return false;
      }
    }

    // Dispatch any existing or new pending events
    wl_display_dispatch_pending(display_internal.get());
    return true;
  }

  wl_registry *display_t::registry() {
    return wl_display_get_registry(display_internal.get());
  }

  inline monitor_t::monitor_t(wl_output *output):
      output {output},
      wl_listener {
        &CLASS_CALL(monitor_t, wl_geometry),
        &CLASS_CALL(monitor_t, wl_mode),
        &CLASS_CALL(monitor_t, wl_done),
        &CLASS_CALL(monitor_t, wl_scale),
      },
      xdg_listener {
        &CLASS_CALL(monitor_t, xdg_position),
        &CLASS_CALL(monitor_t, xdg_size),
        &CLASS_CALL(monitor_t, xdg_done),
        &CLASS_CALL(monitor_t, xdg_name),
        &CLASS_CALL(monitor_t, xdg_description)
      } {
  }

  inline void monitor_t::xdg_name(zxdg_output_v1 *, const char *name) {
    this->name = name;

    BOOST_LOG(info) << "Name: "sv << this->name;
  }

  void monitor_t::xdg_description(zxdg_output_v1 *, const char *description) {
    this->description = description;

    BOOST_LOG(info) << "Found monitor: "sv << this->description;
  }

  void monitor_t::xdg_position(zxdg_output_v1 *, std::int32_t x, std::int32_t y) {
    viewport.offset_x = x;
    viewport.offset_y = y;

    BOOST_LOG(info) << "Offset: "sv << x << 'x' << y;
  }

  void monitor_t::xdg_size(zxdg_output_v1 *, std::int32_t width, std::int32_t height) {
    viewport.logical_width = width;
    viewport.logical_height = height;
    BOOST_LOG(info) << "Logical size: "sv << width << 'x' << height;
  }

  void monitor_t::wl_mode(
    wl_output *wl_output,
    std::uint32_t flags,
    std::int32_t width,
    std::int32_t height,
    std::int32_t refresh
  ) {
    viewport.width = width;
    viewport.height = height;

    BOOST_LOG(info) << "Resolution: "sv << width << 'x' << height;
  }

  void monitor_t::listen(zxdg_output_manager_v1 *output_manager) {
    auto xdg_output = zxdg_output_manager_v1_get_xdg_output(output_manager, output);
    zxdg_output_v1_add_listener(xdg_output, &xdg_listener, this);
    wl_output_add_listener(output, &wl_listener, this);
  }

  interface_t::interface_t() noexcept
      :
      screencopy_manager {nullptr},
      dmabuf_interface {nullptr},
      output_manager {nullptr},
      icc_manager {nullptr},
      icc_source_manager {nullptr},
      listener {
        &CLASS_CALL(interface_t, add_interface),
        &CLASS_CALL(interface_t, del_interface)
      } {
  }

  void interface_t::listen(wl_registry *registry) {
    wl_registry_add_listener(registry, &listener, this);
  }

  void interface_t::add_interface(
    wl_registry *registry,
    std::uint32_t id,
    const char *interface,
    std::uint32_t version
  ) {
    BOOST_LOG(debug) << "Available interface: "sv << interface << '(' << id << ") version "sv << version;

    const auto *wayland_backend = std::getenv("SUNSHINE_WAYLAND_BACKEND");

    if (!std::strcmp(interface, wl_output_interface.name)) {
      BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
      monitors.emplace_back(
        std::make_unique<monitor_t>(
          (wl_output *) wl_registry_bind(registry, id, &wl_output_interface, 2)
        )
      );
    } else if (!std::strcmp(interface, zxdg_output_manager_v1_interface.name)) {
      BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
      output_manager = (zxdg_output_manager_v1 *) wl_registry_bind(registry, id, &zxdg_output_manager_v1_interface, version);

      this->interface[XDG_OUTPUT] = true;
    } else if (!std::strcmp(interface, zwlr_screencopy_manager_v1_interface.name) && (!wayland_backend || !strcmp(wayland_backend, "screencopy"))) {
      BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
      screencopy_manager = (zwlr_screencopy_manager_v1 *) wl_registry_bind(registry, id, &zwlr_screencopy_manager_v1_interface, version);

      this->interface[WLR_SCREENCOPY] = true;
    } else if (!std::strcmp(interface, zwp_linux_dmabuf_v1_interface.name)) {
      BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
      dmabuf_interface = (zwp_linux_dmabuf_v1 *) wl_registry_bind(registry, id, &zwp_linux_dmabuf_v1_interface, version);

      this->interface[LINUX_DMABUF] = true;
    } else if (!std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) && (!wayland_backend || !strcmp(wayland_backend, "icc"))) {
      BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
      icc_manager = (ext_image_copy_capture_manager_v1 *) wl_registry_bind(registry, id, &ext_image_copy_capture_manager_v1_interface, version);

      this->interface[EXT_IMAGE_COPY_CAPTURE] = true;
    } else if (!std::strcmp(interface, ext_output_image_capture_source_manager_v1_interface.name) && (!wayland_backend || !strcmp(wayland_backend, "icc"))) {
      BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
      icc_source_manager = (ext_output_image_capture_source_manager_v1 *) wl_registry_bind(registry, id, &ext_output_image_capture_source_manager_v1_interface, version);

      this->interface[EXT_IMAGE_CAPTURE_SOURCE] = true;
    }
  }

  void interface_t::del_interface(wl_registry *registry, uint32_t id) {
    BOOST_LOG(info) << "Delete: "sv << id;
  }

  // Initialize GBM
  bool dmabuf_t::init_gbm() {
    if (gbm_device) {
      return true;
    }

    // We don't have a drm device from the protocol for some reason
    // This should never happen, but this code was already here so it's a fallback now

    auto node = config::video.adapter_name;
    int drm_fd = -1;
    if (!node.empty()) {
      // Prefer adapter_name from config
      drm_fd = open(node.c_str(), O_RDWR);
    }

    if (drm_fd < 0) {
      // Find any render node and hope that works
      drmDevice *devices[16];
      int n = drmGetDevices2(0, devices, 16);
      if (n <= 0) {
        BOOST_LOG(error) << "No DRM devices found"sv;
        return false;
      }

      int drm_fd = -1;
      for (int i = 0; i < n; i++) {
        if (devices[i]->available_nodes & (1 << DRM_NODE_RENDER)) {
          drm_fd = open(devices[i]->nodes[DRM_NODE_RENDER], O_RDWR);
          if (drm_fd >= 0) {
            node = std::string {devices[i]->nodes[DRM_NODE_RENDER]};
            break;
          }
        }
      }
      drmFreeDevices(devices, n);
    }

    if (drm_fd < 0) {
      BOOST_LOG(error) << "Failed to open DRM render node: " << node;
      return false;
    } else {
      BOOST_LOG(info) << "Using DRM render node: " << node;
    }

    gbm_device = gbm_create_device(drm_fd);
    if (!gbm_device) {
      close(drm_fd);
      BOOST_LOG(error) << "Failed to create GBM device"sv;
      return false;
    }

    return true;
  }

  // Cleanup GBM
  void dmabuf_t::cleanup_gbm() {
    if (current_bo) {
      gbm_bo_destroy(current_bo);
      current_bo = nullptr;
    }

    if (current_wl_buffer) {
      wl_buffer_destroy(current_wl_buffer);
      current_wl_buffer = nullptr;
    }
  }

  dmabuf_t::dmabuf_t():
      status {READY},
      frames {},
      current_frame {&frames[0]},
      listener {
        &CLASS_CALL(dmabuf_t, buffer),
        &CLASS_CALL(dmabuf_t, flags),
        &CLASS_CALL(dmabuf_t, ready),
        &CLASS_CALL(dmabuf_t, failed),
        &CLASS_CALL(dmabuf_t, damage),
        &CLASS_CALL(dmabuf_t, linux_dmabuf),
        &CLASS_CALL(dmabuf_t, buffer_done),
      } {
  }

  // Start capture
  void dmabuf_t::screencopy_create(
    zwlr_screencopy_manager_v1 *screencopy_manager,
    zwp_linux_dmabuf_v1 *dmabuf_interface,
    wl_output *output
  ) {
    this->dmabuf_interface = dmabuf_interface;
    // Reset state
    shm_info.supported = false;
    dmabuf_info.supported = false;
    screencopy_session = {};

    screencopy_session.manager = screencopy_manager;
    screencopy_session.output = output;

    // Get feedback for correct drm device to use
    auto feedback = zwp_linux_dmabuf_v1_get_default_feedback(dmabuf_interface);
    zwp_linux_dmabuf_feedback_v1_set_user_data(feedback, this);
    zwp_linux_dmabuf_feedback_v1_add_listener(feedback, &feedback_listener, this);
  }

  void dmabuf_t::screencopy_capture(bool blend_cursor) {
    if (!screencopy_session.done) {
      status = REINIT;
      return;
    } else if (screencopy_session.manager && screencopy_session.output && screencopy_session.done && !screencopy_session.frame) {
      // Create new frame
      auto frame = zwlr_screencopy_manager_v1_capture_output(
        screencopy_session.manager,
        blend_cursor ? 1 : 0,
        screencopy_session.output
      );

      // Store frame data pointer for callbacks
      zwlr_screencopy_frame_v1_set_user_data(frame, this);

      // Add listener
      zwlr_screencopy_frame_v1_add_listener(frame, &listener, this);

      screencopy_session.frame = frame;

      status = WAITING;
    }
  }

  dmabuf_t::~dmabuf_t() {
    cleanup_gbm();

    for (auto &frame : frames) {
      frame.destroy();
    }

    if (gbm_device) {
      // We should close the DRM FD, but it's owned by GBM
      gbm_device_destroy(gbm_device);
      gbm_device = nullptr;
    }
  }

  // Buffer format callback
  void dmabuf_t::buffer(
    zwlr_screencopy_frame_v1 *frame,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t stride
  ) {
    shm_info.supported = true;
    shm_info.format = format;
    shm_info.width = width;
    shm_info.height = height;
    shm_info.stride = stride;

    BOOST_LOG(debug) << "Screencopy supports SHM format: "sv << format;
  }

  // DMA-BUF format callback
  void dmabuf_t::linux_dmabuf(
    zwlr_screencopy_frame_v1 *frame,
    std::uint32_t format,
    std::uint32_t width,
    std::uint32_t height
  ) {
    dmabuf_info.supported = true;
    dmabuf_info.format = format;
    dmabuf_info.width = width;
    dmabuf_info.height = height;

    BOOST_LOG(debug) << "Screencopy supports DMA-BUF format: "sv << format;
  }

  // Flags callback
  void dmabuf_t::flags(zwlr_screencopy_frame_v1 *frame, std::uint32_t flags) {
    y_invert = flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT;
    BOOST_LOG(debug) << "Frame flags: "sv << flags << (y_invert ? " (y_invert)" : "");
  }

  // DMA-BUF creation helper
  void dmabuf_t::create_and_copy_dmabuf(zwlr_screencopy_frame_v1 *frame) {
    if (!init_gbm()) {
      BOOST_LOG(error) << "Failed to initialize GBM"sv;
      zwlr_screencopy_frame_v1_destroy(frame);
      status = REINIT;
      return;
    }

    // Create GBM buffer
    current_bo = gbm_bo_create(gbm_device, dmabuf_info.width, dmabuf_info.height, dmabuf_info.format, GBM_BO_USE_RENDERING);
    if (!current_bo) {
      BOOST_LOG(error) << "Failed to create GBM buffer"sv;
      zwlr_screencopy_frame_v1_destroy(frame);
      status = REINIT;
      return;
    }

    // Get buffer info
    int fd = gbm_bo_get_fd(current_bo);
    if (fd < 0) {
      BOOST_LOG(error) << "Failed to get buffer FD"sv;
      gbm_bo_destroy(current_bo);
      current_bo = nullptr;
      zwlr_screencopy_frame_v1_destroy(frame);
      status = REINIT;
      return;
    }

    uint32_t stride = gbm_bo_get_stride(current_bo);
    uint64_t modifier = gbm_bo_get_modifier(current_bo);

    // Store in surface descriptor for later use
    auto next_frame = get_next_frame();
    next_frame->sd.fds[0] = fd;
    next_frame->sd.pitches[0] = stride;
    next_frame->sd.offsets[0] = 0;
    next_frame->sd.modifier = modifier;

    // Create linux-dmabuf buffer
    auto params = zwp_linux_dmabuf_v1_create_params(dmabuf_interface);
    zwp_linux_buffer_params_v1_add(params, fd, 0, 0, stride, modifier >> 32, modifier & 0xffffffff);
    zwp_linux_buffer_params_v1_set_user_data(params, this);
    zwp_linux_buffer_params_v1_add_listener(params, &params_listener, this);

    // Create Wayland buffer (async - callback will handle copy)
    zwp_linux_buffer_params_v1_create(params, dmabuf_info.width, dmabuf_info.height, dmabuf_info.format, 0);
  }

  // Buffer done callback - time to create buffer
  void dmabuf_t::buffer_done(zwlr_screencopy_frame_v1 *frame) {
    auto next_frame = get_next_frame();

    // Prefer DMA-BUF if supported
    if (dmabuf_info.supported && dmabuf_interface) {
      // Store format info first
      next_frame->sd.fourcc = dmabuf_info.format;
      next_frame->sd.width = dmabuf_info.width;
      next_frame->sd.height = dmabuf_info.height;

      // Create and start copy
      create_and_copy_dmabuf(frame);
    } else if (shm_info.supported) {
      // SHM fallback would go here
      BOOST_LOG(warning) << "SHM capture not implemented"sv;
      zwlr_screencopy_frame_v1_destroy(frame);
      status = REINIT;
    } else {
      BOOST_LOG(error) << "No supported buffer types"sv;
      zwlr_screencopy_frame_v1_destroy(frame);
      status = REINIT;
    }
  }

  // Buffer params created callback
  void dmabuf_t::buffer_params_created(
    zwp_linux_buffer_params_v1 *params,
    struct wl_buffer *buffer
  ) {
    // Store for cleanup
    current_wl_buffer = buffer;

    // Start the actual copy
    zwp_linux_buffer_params_v1_destroy(params);
    zwlr_screencopy_frame_v1_copy(screencopy_session.frame, buffer);
  }

  // Buffer params failed callback
  void dmabuf_t::buffer_params_failed(
    zwp_linux_buffer_params_v1 *params
  ) {
    BOOST_LOG(error) << "Failed to create buffer from params"sv;
    cleanup_gbm();

    zwp_linux_buffer_params_v1_destroy(params);
    zwlr_screencopy_frame_v1_destroy(screencopy_session.frame);
    status = REINIT;
  }

  void dmabuf_t::buffer_feedback_done(zwp_linux_dmabuf_feedback_v1 *feedback) {
    screencopy_session.done = true;
    status = WAITING;
  }

  void dmabuf_t::buffer_feedback_format_table(zwp_linux_dmabuf_feedback_v1 *feedback, int fd, int size) {};

  void dmabuf_t::buffer_feedback_main_device(zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *device) {
    if (device->size != sizeof(dev_t)) {
      BOOST_LOG(error) << "Screencopy DMA-BUF device size mismatch: " << device->size << " != " << sizeof(dev_t);
      status = REINIT;
      return;
    }

    dev_t device_id;
    memcpy(&device_id, device->data, sizeof(dev_t));

    drmDevice *drm_device {nullptr};
    if (drmGetDeviceFromDevId(device_id, 0, &drm_device) != 0) {
      BOOST_LOG(error) << "Screencopy failed to open DRM device";
      status = REINIT;
      return;
    }

    char *node {nullptr};
    if (drm_device->available_nodes & (1 << DRM_NODE_RENDER)) {
      node = drm_device->nodes[DRM_NODE_RENDER];
    } else if (drm_device->available_nodes & (1 << DRM_NODE_PRIMARY)) {
      node = drm_device->nodes[DRM_NODE_PRIMARY];
    } else {
      BOOST_LOG(error) << "Screencopy failed to find DRM node";
      status = REINIT;
      return;
    }

    int drm_fd = open(node, O_RDWR);
    if (drm_fd < 0) {
      BOOST_LOG(error) << "Screencopy failed to open DRM render node";
      status = REINIT;
      return;
    }

    gbm_device = gbm_create_device(drm_fd);
    if (!gbm_device) {
      close(drm_fd);
      BOOST_LOG(error) << "Screencopy failed to create GBM device";
      status = REINIT;
      return;
    }

    BOOST_LOG(info) << "Screencopy DMA-BUF device: " << node;
  }

  void dmabuf_t::buffer_feedback_tranche_done(zwp_linux_dmabuf_feedback_v1 *feedback) {};
  void dmabuf_t::buffer_feedback_tranche_target_device(zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *device) {};
  void dmabuf_t::buffer_feedback_tranche_formats(zwp_linux_dmabuf_feedback_v1 *feedback, struct wl_array *formats) {};
  void dmabuf_t::buffer_feedback_tranche_flags(zwp_linux_dmabuf_feedback_v1 *feedback, int flags) {};

  // Ready callback
  void dmabuf_t::ready(
    zwlr_screencopy_frame_v1 *frame,
    std::uint32_t tv_sec_hi,
    std::uint32_t tv_sec_lo,
    std::uint32_t tv_nsec
  ) {
    BOOST_LOG(debug) << "Frame ready"sv;

    // Frame is ready for use, GBM buffer now contains screen content
    current_frame->destroy();
    current_frame = get_next_frame();

    // Keep the GBM buffer alive but destroy the Wayland objects
    if (current_wl_buffer) {
      wl_buffer_destroy(current_wl_buffer);
      current_wl_buffer = nullptr;
    }

    cleanup_gbm();

    zwlr_screencopy_frame_v1_destroy(frame);
    screencopy_session.frame = nullptr;
    status = READY;
  }

  // Failed callback
  void dmabuf_t::failed(zwlr_screencopy_frame_v1 *frame) {
    BOOST_LOG(error) << "Frame capture failed"sv;

    // Clean up resources
    cleanup_gbm();
    auto next_frame = get_next_frame();
    next_frame->destroy();

    zwlr_screencopy_frame_v1_destroy(frame);
    status = REINIT;
  }

  void dmabuf_t::damage(
    zwlr_screencopy_frame_v1 *frame,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height
  ) {};

  void frame_t::destroy() {
    for (auto x = 0; x < 4; ++x) {
      if (sd.fds[x] >= 0) {
        close(sd.fds[x]);

        sd.fds[x] = -1;
      }
    }
  }

  frame_t::frame_t() {
    // File descriptors aren't open
    std::fill_n(sd.fds, 4, -1);
  };

  std::vector<std::unique_ptr<monitor_t>> monitors(const char *display_name) {
    display_t display;

    if (display.init(display_name)) {
      return {};
    }

    interface_t interface;
    interface.listen(display.registry());

    display.roundtrip();

    if (!interface[interface_t::XDG_OUTPUT]) {
      BOOST_LOG(error) << "Missing Wayland wire XDG_OUTPUT"sv;
      return {};
    }

    for (auto &monitor : interface.monitors) {
      monitor->listen(interface.output_manager);
    }

    display.roundtrip();

    return std::move(interface.monitors);
  }

  static bool validate() {
    display_t display;

    return display.init() == 0;
  }

  int init() {
    static bool validated = validate();

    return !validated;
  }

  // ext-image-copy-capture implementation starts here

  static const struct zwp_linux_buffer_params_v1_listener icc_params_listener = {
    .created = CLASS_CALL(dmabuf_t, icc_buffer_params_created),
    .failed = CLASS_CALL(dmabuf_t, icc_buffer_params_failed),
  };

  static const struct ext_image_copy_capture_session_v1_listener icc_session_listener = {
    .buffer_size = CLASS_CALL(dmabuf_t, icc_session_buffer_size),
    .shm_format = CLASS_CALL(dmabuf_t, icc_session_shm_format),
    .dmabuf_device = CLASS_CALL(dmabuf_t, icc_session_dmabuf_device),
    .dmabuf_format = CLASS_CALL(dmabuf_t, icc_session_dmabuf_format),
    .done = CLASS_CALL(dmabuf_t, icc_session_done),
    .stopped = CLASS_CALL(dmabuf_t, icc_session_stopped),
  };

  static const struct ext_image_copy_capture_frame_v1_listener icc_frame_listener = {
    .transform = CLASS_CALL(dmabuf_t, icc_frame_transform),
    .damage = CLASS_CALL(dmabuf_t, icc_frame_damage),
    .presentation_time = CLASS_CALL(dmabuf_t, icc_frame_presentation_time),
    .ready = CLASS_CALL(dmabuf_t, icc_frame_ready),
    .failed = CLASS_CALL(dmabuf_t, icc_frame_failed),
  };

  void dmabuf_t::icc_create(
    ext_image_copy_capture_manager_v1 *manager,
    ext_output_image_capture_source_manager_v1 *source_manager,
    zwp_linux_dmabuf_v1 *dmabuf_interface,
    wl_output *output,
    bool blend_cursor
  ) {
    this->dmabuf_interface = dmabuf_interface;
    icc_cleanup();
    icc_session.cursor = blend_cursor;

    auto icc_source = ext_output_image_capture_source_manager_v1_create_source(source_manager, output);
    auto session = ext_image_copy_capture_manager_v1_create_session(manager, icc_source, blend_cursor ? EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS : 0);
    ext_image_copy_capture_session_v1_set_user_data(session, this);
    ext_image_copy_capture_session_v1_add_listener(session, &icc_session_listener, this);
    icc_session.session = session;
    status = WAITING;
  }

  void dmabuf_t::icc_capture(bool blend_cursor) {
    if (!icc_session.session || blend_cursor != icc_session.cursor) {
      status = REINIT;
      return;
    } else if (icc_session.done && !icc_session.frame) {
      auto frame = ext_image_copy_capture_session_v1_create_frame(icc_session.session);
      icc_session.frame = frame;
      ext_image_copy_capture_frame_v1_set_user_data(frame, this);
      ext_image_copy_capture_frame_v1_add_listener(frame, &icc_frame_listener, this);
      icc_create_and_copy_dmabuf(frame);
      status = WAITING;
    }
  }

  void dmabuf_t::icc_create_and_copy_dmabuf(ext_image_copy_capture_frame_v1 *frame) {
    if (!init_gbm()) {
      BOOST_LOG(error) << "Failed to initialize GBM"sv;
      ext_image_copy_capture_frame_v1_destroy(frame);
      status = REINIT;
      return;
    }

    if (icc_session.width == 0 || icc_session.height == 0 || icc_session.format == 0) {
      BOOST_LOG(error) << "Invalid capture session parameters"sv;
      ext_image_copy_capture_frame_v1_destroy(frame);
      status = REINIT;
      return;
    }

    current_bo = gbm_bo_create(gbm_device, icc_session.width, icc_session.height, icc_session.format, GBM_BO_USE_RENDERING);
    if (!current_bo) {
      BOOST_LOG(error) << "Failed to create GBM buffer"sv;
      ext_image_copy_capture_frame_v1_destroy(frame);
      status = REINIT;
      return;
    }

    int fd = gbm_bo_get_fd(current_bo);
    if (fd < 0) {
      BOOST_LOG(error) << "Failed to get buffer FD"sv;
      gbm_bo_destroy(current_bo);
      current_bo = nullptr;
      ext_image_copy_capture_frame_v1_destroy(frame);
      status = REINIT;
      return;
    }

    uint32_t stride = gbm_bo_get_stride(current_bo);
    auto next_frame = get_next_frame();
    next_frame->sd.fds[0] = fd;
    next_frame->sd.pitches[0] = stride;
    next_frame->sd.offsets[0] = 0;
    next_frame->sd.modifier = icc_session.num_modifiers > 0 ? icc_session.modifiers[icc_session.modifier] : DRM_FORMAT_MOD_LINEAR;
    next_frame->sd.fourcc = icc_session.format;
    next_frame->sd.width = icc_session.width;
    next_frame->sd.height = icc_session.height;

    auto params = zwp_linux_dmabuf_v1_create_params(dmabuf_interface);
    zwp_linux_buffer_params_v1_add(params, fd, 0, 0, stride, next_frame->sd.modifier >> 32, next_frame->sd.modifier & 0xffffffff);
    zwp_linux_buffer_params_v1_set_user_data(params, this);
    zwp_linux_buffer_params_v1_add_listener(params, &icc_params_listener, this);
    zwp_linux_buffer_params_v1_create(params, icc_session.width, icc_session.height, icc_session.format, 0);
  }

  void dmabuf_t::icc_buffer_params_created(
    zwp_linux_buffer_params_v1 *params,
    struct wl_buffer *buffer
  ) {
    current_wl_buffer = buffer;

    zwp_linux_buffer_params_v1_destroy(params);
    if (icc_session.frame) {
      ext_image_copy_capture_frame_v1_attach_buffer(icc_session.frame, buffer);
      ext_image_copy_capture_frame_v1_damage_buffer(icc_session.frame, 0, 0, icc_session.width, icc_session.height);
      ext_image_copy_capture_frame_v1_capture(icc_session.frame);
    }
  }

  void dmabuf_t::icc_buffer_params_failed(
    zwp_linux_buffer_params_v1 *params
  ) {
    BOOST_LOG(error) << "Failed to create buffer for ICC"sv;

    cleanup_gbm();
    status = REINIT;

    zwp_linux_buffer_params_v1_destroy(params);
    if (icc_session.frame) {
      ext_image_copy_capture_frame_v1_destroy(icc_session.frame);
      icc_session.frame = nullptr;
    }
  }

  void dmabuf_t::icc_session_buffer_size(ext_image_copy_capture_session_v1 *session, std::uint32_t width, std::uint32_t height) {
    icc_session.width = width;
    icc_session.height = height;
    BOOST_LOG(info) << "ICC session buffer size: " << width << 'x' << height;
  }

  void dmabuf_t::icc_session_shm_format(ext_image_copy_capture_session_v1 *session, std::uint32_t format) {
    shm_info.supported = true;
    shm_info.format = format;
    BOOST_LOG(debug) << "ICC session SHM format: " << format;
  }

  void dmabuf_t::icc_session_dmabuf_device(ext_image_copy_capture_session_v1 *session, struct wl_array *device) {
    if (device->size != sizeof(dev_t)) {
      BOOST_LOG(error) << "ICC session DMA-BUF device size mismatch: " << device->size << " != " << sizeof(dev_t);
      status = REINIT;
      return;
    }

    dev_t device_id;
    memcpy(&device_id, device->data, sizeof(dev_t));

    drmDevice *drm_device {nullptr};
    if (drmGetDeviceFromDevId(device_id, 0, &drm_device) != 0) {
      BOOST_LOG(info) << "ICC session failed to open DRM device";
      status = REINIT;
      return;
    }

    char *node {nullptr};
    if (drm_device->available_nodes & (1 << DRM_NODE_RENDER)) {
      node = drm_device->nodes[DRM_NODE_RENDER];
    } else if (drm_device->available_nodes & (1 << DRM_NODE_PRIMARY)) {
      node = drm_device->nodes[DRM_NODE_PRIMARY];
    } else {
      BOOST_LOG(info) << "ICC session failed to find DRM node";
      status = REINIT;
      return;
    }

    int drm_fd = open(node, O_RDWR);
    if (drm_fd < 0) {
      BOOST_LOG(error) << "ICC session failed to open DRM render node";
      status = REINIT;
      return;
    }

    gbm_device = gbm_create_device(drm_fd);
    if (!gbm_device) {
      close(drm_fd);
      BOOST_LOG(error) << "ICC session failed to create GBM device";
      status = REINIT;
      return;
    }

    BOOST_LOG(info) << "ICC session DMA-BUF device: " << node;
  }

  void dmabuf_t::icc_session_dmabuf_format(ext_image_copy_capture_session_v1 *session, std::uint32_t format, struct wl_array *modifiers) {
    char fourcc[5] {0};
    memcpy(&fourcc, &format, 4);
    BOOST_LOG(info) << "ICC session DMA-BUF format: " << std::hex << format << " = " << fourcc;
    if (fourcc[0] != 'X') {
      return;
    }
    icc_session.dmabuf_supported = true;
    icc_session.format = format;
    if (icc_session.modifiers != nullptr) {
      free(icc_session.modifiers);
      icc_session.modifiers = nullptr;
    }
    icc_session.num_modifiers = modifiers->size / sizeof(uint64_t);
    icc_session.modifiers = (uint64_t *) calloc(icc_session.num_modifiers, sizeof(uint64_t));
    icc_session.modifier = 0;
    if (icc_session.modifiers != nullptr) {
      memcpy(icc_session.modifiers, modifiers->data, modifiers->size);
      for (size_t i = 0; i < icc_session.num_modifiers; ++i) {
        BOOST_LOG(info) << " Modifier " << std::setw(2) << i << ": " << std::hex << icc_session.modifiers[i];
        if (icc_session.modifiers[icc_session.modifier] == DRM_FORMAT_MOD_INVALID) {
          icc_session.modifier = i;
        }
      }
    }
    BOOST_LOG(info) << "Selected modifier " << icc_session.modifier;
  }

  void dmabuf_t::icc_session_done(ext_image_copy_capture_session_v1 *session) {
    BOOST_LOG(debug) << "ICC session constraints done";

    if (!icc_session.session) {
      BOOST_LOG(error) << "No ICC session"sv;
      status = REINIT;
      return;
    }

    if (icc_session.width == 0 || icc_session.height == 0) {
      BOOST_LOG(error) << "Invalid ICC buffer size"sv;
      status = REINIT;
      return;
    }

    if (!icc_session.dmabuf_supported) {
      BOOST_LOG(error) << "ICC session does not support dmabuf"sv;
      status = REINIT;
      return;
    }

    icc_session.done = true;
  }

  void dmabuf_t::icc_cleanup() {
    if (icc_session.modifiers) {
      free(icc_session.modifiers);
    }
    icc_session = {};
  }

  void dmabuf_t::icc_session_stopped(ext_image_copy_capture_session_v1 *session) {
    BOOST_LOG(warning) << "ICC session stopped"sv;
    ext_image_copy_capture_session_v1_destroy(session);
    icc_cleanup();
    status = REINIT;
  }

  void dmabuf_t::icc_frame_transform(ext_image_copy_capture_frame_v1 *frame, std::uint32_t transform) {
    // TODO: this could probably do something
    BOOST_LOG(debug) << "ICC transform: " << transform;
  }

  void dmabuf_t::icc_frame_damage(ext_image_copy_capture_frame_v1 *frame, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    BOOST_LOG(debug) << "ICC damage: " << x << ',' << y << ' ' << width << 'x' << height;
  }

  void dmabuf_t::icc_frame_presentation_time(ext_image_copy_capture_frame_v1 *frame, std::uint32_t tv_sec_hi, std::uint32_t tv_sec_lo, std::uint32_t tv_nsec) {
    uint64_t timestamp = ((uint64_t) tv_sec_hi << 32) | tv_sec_lo;
    BOOST_LOG(debug) << "ICC presentation time: " << timestamp << '.' << tv_nsec;
  }

  void dmabuf_t::icc_frame_ready(ext_image_copy_capture_frame_v1 *frame) {
    BOOST_LOG(debug) << "ICC frame ready"sv;
    current_frame->destroy();
    current_frame = get_next_frame();
    if (current_wl_buffer) {
      wl_buffer_destroy(current_wl_buffer);
      current_wl_buffer = nullptr;
    }
    cleanup_gbm();
    ext_image_copy_capture_frame_v1_destroy(frame);
    icc_session.frame = nullptr;
    status = READY;
  }

  void dmabuf_t::icc_frame_failed(ext_image_copy_capture_frame_v1 *frame, std::uint32_t reason) {
    if (reason == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED || icc_session.fail_count > icc_session.num_modifiers) {
      BOOST_LOG(error) << "ICC frame failed: Session has stopped or too many failures, restarting";
      cleanup_gbm();
      auto next_frame = get_next_frame();
      next_frame->destroy();
      ext_image_copy_capture_frame_v1_destroy(frame);
      icc_cleanup();
      status = REINIT;
      return;
    } else if (reason == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS) {
      BOOST_LOG(error) << "ICC frame failed: Buffer constraints";
    } else {
      BOOST_LOG(error) << "ICC frame failed: Unknown reason";
    }
    ++icc_session.fail_count;
    if (icc_session.num_modifiers > 1) {
      icc_session.modifier = (icc_session.modifier + 1) % icc_session.num_modifiers;
      BOOST_LOG(info) << "Retrying with modifier " << icc_session.modifier;
    }
    ext_image_copy_capture_frame_v1_destroy(frame);
    icc_session.frame = nullptr;
    icc_capture(icc_session.cursor);
  }

}  // namespace wl

#pragma GCC diagnostic pop
