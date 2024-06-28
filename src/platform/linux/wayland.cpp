/**
 * @file src/platform/linux/wayland.cpp
 * @brief Definitions for Wayland capture.
 */
#include <poll.h>
#include <wayland-client.h>
#include <wayland-util.h>

#include <cstdlib>

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
  template <class T, class Method, Method m, class... Params>
  static auto
  classCall(void *data, Params... params) -> decltype(((*reinterpret_cast<T *>(data)).*m)(params...)) {
    return ((*reinterpret_cast<T *>(data)).*m)(params...);
  }

#define CLASS_CALL(c, m) classCall<c, decltype(&c::m), &c::m>

  int
  display_t::init(const char *display_name) {
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

  void
  display_t::roundtrip() {
    wl_display_roundtrip(display_internal.get());
  }

  /**
   * @brief Waits up to the specified timeout to dispatch new events on the wl_display.
   * @param timeout The timeout in milliseconds.
   * @return `true` if new events were dispatched or `false` if the timeout expired.
   */
  bool
  display_t::dispatch(std::chrono::milliseconds timeout) {
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
      }
      else {
        // We timed out, so unlock the queue now
        wl_display_cancel_read(display_internal.get());
        return false;
      }
    }

    // Dispatch any existing or new pending events
    wl_display_dispatch_pending(display_internal.get());
    return true;
  }

  wl_registry *
  display_t::registry() {
    return wl_display_get_registry(display_internal.get());
  }

  inline monitor_t::monitor_t(wl_output *output):
      output { output },
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
      } {}

  inline void
  monitor_t::xdg_name(zxdg_output_v1 *, const char *name) {
    this->name = name;

    BOOST_LOG(info) << "Name: "sv << this->name;
  }

  void
  monitor_t::xdg_description(zxdg_output_v1 *, const char *description) {
    this->description = description;

    BOOST_LOG(info) << "Found monitor: "sv << this->description;
  }

  void
  monitor_t::xdg_position(zxdg_output_v1 *, std::int32_t x, std::int32_t y) {
    viewport.offset_x = x;
    viewport.offset_y = y;

    BOOST_LOG(info) << "Offset: "sv << x << 'x' << y;
  }

  void
  monitor_t::xdg_size(zxdg_output_v1 *, std::int32_t width, std::int32_t height) {
    BOOST_LOG(info) << "Logical size: "sv << width << 'x' << height;
  }

  void
  monitor_t::wl_mode(wl_output *wl_output, std::uint32_t flags,
    std::int32_t width, std::int32_t height, std::int32_t refresh) {
    viewport.width = width;
    viewport.height = height;

    BOOST_LOG(info) << "Resolution: "sv << width << 'x' << height;
  }

  void
  monitor_t::listen(zxdg_output_manager_v1 *output_manager) {
    auto xdg_output = zxdg_output_manager_v1_get_xdg_output(output_manager, output);
    zxdg_output_v1_add_listener(xdg_output, &xdg_listener, this);
    wl_output_add_listener(output, &wl_listener, this);
  }

  interface_t::interface_t() noexcept
      :
      output_manager { nullptr },
      listener {
        &CLASS_CALL(interface_t, add_interface),
        &CLASS_CALL(interface_t, del_interface)
      } {}

  void
  interface_t::listen(wl_registry *registry) {
    wl_registry_add_listener(registry, &listener, this);
  }

  void
  interface_t::add_interface(wl_registry *registry, std::uint32_t id, const char *interface, std::uint32_t version) {
    BOOST_LOG(debug) << "Available interface: "sv << interface << '(' << id << ") version "sv << version;

    if (!std::strcmp(interface, wl_output_interface.name)) {
      BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
      monitors.emplace_back(
        std::make_unique<monitor_t>(
          (wl_output *) wl_registry_bind(registry, id, &wl_output_interface, 2)));
    }
    else if (!std::strcmp(interface, zxdg_output_manager_v1_interface.name)) {
      BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
      output_manager = (zxdg_output_manager_v1 *) wl_registry_bind(registry, id, &zxdg_output_manager_v1_interface, version);

      this->interface[XDG_OUTPUT] = true;
    }
    else if (!std::strcmp(interface, zwlr_export_dmabuf_manager_v1_interface.name)) {
      BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
      dmabuf_manager = (zwlr_export_dmabuf_manager_v1 *) wl_registry_bind(registry, id, &zwlr_export_dmabuf_manager_v1_interface, version);

      this->interface[WLR_EXPORT_DMABUF] = true;
    }
  }

  void
  interface_t::del_interface(wl_registry *registry, uint32_t id) {
    BOOST_LOG(info) << "Delete: "sv << id;
  }

  dmabuf_t::dmabuf_t():
      status { READY }, frames {}, current_frame { &frames[0] }, listener {
        &CLASS_CALL(dmabuf_t, frame),
        &CLASS_CALL(dmabuf_t, object),
        &CLASS_CALL(dmabuf_t, ready),
        &CLASS_CALL(dmabuf_t, cancel)
      } {
  }

  void
  dmabuf_t::listen(zwlr_export_dmabuf_manager_v1 *dmabuf_manager, wl_output *output, bool blend_cursor) {
    auto frame = zwlr_export_dmabuf_manager_v1_capture_output(dmabuf_manager, blend_cursor, output);
    zwlr_export_dmabuf_frame_v1_add_listener(frame, &listener, this);

    status = WAITING;
  }

  dmabuf_t::~dmabuf_t() {
    for (auto &frame : frames) {
      frame.destroy();
    }
  }

  void
  dmabuf_t::frame(
    zwlr_export_dmabuf_frame_v1 *frame,
    std::uint32_t width, std::uint32_t height,
    std::uint32_t x, std::uint32_t y,
    std::uint32_t buffer_flags, std::uint32_t flags,
    std::uint32_t format,
    std::uint32_t high, std::uint32_t low,
    std::uint32_t obj_count) {
    auto next_frame = get_next_frame();

    next_frame->sd.fourcc = format;
    next_frame->sd.width = width;
    next_frame->sd.height = height;
    next_frame->sd.modifier = (((std::uint64_t) high) << 32) | low;
  }

  void
  dmabuf_t::object(
    zwlr_export_dmabuf_frame_v1 *frame,
    std::uint32_t index,
    std::int32_t fd,
    std::uint32_t size,
    std::uint32_t offset,
    std::uint32_t stride,
    std::uint32_t plane_index) {
    auto next_frame = get_next_frame();

    next_frame->sd.fds[plane_index] = fd;
    next_frame->sd.pitches[plane_index] = stride;
    next_frame->sd.offsets[plane_index] = offset;
  }

  void
  dmabuf_t::ready(
    zwlr_export_dmabuf_frame_v1 *frame,
    std::uint32_t tv_sec_hi, std::uint32_t tv_sec_lo, std::uint32_t tv_nsec) {
    zwlr_export_dmabuf_frame_v1_destroy(frame);

    current_frame->destroy();
    current_frame = get_next_frame();

    status = READY;
  }

  void
  dmabuf_t::cancel(
    zwlr_export_dmabuf_frame_v1 *frame,
    std::uint32_t reason) {
    zwlr_export_dmabuf_frame_v1_destroy(frame);

    auto next_frame = get_next_frame();
    next_frame->destroy();

    status = REINIT;
  }

  void
  frame_t::destroy() {
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

  std::vector<std::unique_ptr<monitor_t>>
  monitors(const char *display_name) {
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

  static bool
  validate() {
    display_t display;

    return display.init() == 0;
  }

  int
  init() {
    static bool validated = validate();

    return !validated;
  }

}  // namespace wl

#pragma GCC diagnostic pop