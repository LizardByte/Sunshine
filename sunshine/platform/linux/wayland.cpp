#include <wayland-client.h>
#include <wayland-util.h>

#include <cstdlib>

#include "graphics.h"
#include "sunshine/main.h"
#include "sunshine/platform/common.h"
#include "sunshine/round_robin.h"
#include "sunshine/utility.h"
#include "wayland.h"

extern const wl_interface wl_output_interface;

using namespace std::literals;

// Disable warning for converting incompatible functions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wpmf-conversions"

namespace wl {
int display_t::init(const char *display_name) {
  if(!display_name) {
    display_name = std::getenv("WAYLAND_DISPLAY");
  }

  if(!display_name) {
    BOOST_LOG(error) << "Environment variable WAYLAND_DISPLAY has not been defined"sv;
    return -1;
  }

  display_internal.reset(wl_display_connect(display_name));
  if(!display_internal) {
    BOOST_LOG(error) << "Couldn't connect to Wayland display: "sv << display_name;
    return -1;
  }

  BOOST_LOG(info) << "Found display ["sv << display_name << ']';

  return 0;
}

void display_t::roundtrip() {
  wl_display_roundtrip(display_internal.get());
}

wl_registry *display_t::registry() {
  return wl_display_get_registry(display_internal.get());
}

inline monitor_t::monitor_t(wl_output *output) : output { output } {}

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
  viewport.width  = width;
  viewport.height = height;

  BOOST_LOG(info) << "Resolution: "sv << width << 'x' << height;
}

void monitor_t::xdg_done(zxdg_output_v1 *) {
  BOOST_LOG(info) << "All info about monitor ["sv << name << "] has been send"sv;
}

void monitor_t::listen(zxdg_output_manager_v1 *output_manager) {
  auto xdg_output = zxdg_output_manager_v1_get_xdg_output(output_manager, output);

#define CLASS_CALL(x, y) x = (decltype(x))&y

  CLASS_CALL(listener.name, monitor_t::xdg_name);
  CLASS_CALL(listener.logical_size, monitor_t::xdg_size);
  CLASS_CALL(listener.logical_position, monitor_t::xdg_position);
  CLASS_CALL(listener.done, monitor_t::xdg_done);
  CLASS_CALL(listener.description, monitor_t::xdg_description);

#undef CLASS_CALL
  zxdg_output_v1_add_listener(xdg_output, &listener, this);
}

interface_t::interface_t() noexcept
    : output_manager { nullptr }, listener {
        (decltype(wl_registry_listener::global))&interface_t::add_interface,
        (decltype(wl_registry_listener::global_remove))&interface_t::del_interface,
      } {}

void interface_t::listen(wl_registry *registry) {
  wl_registry_add_listener(registry, &listener, this);
}

void interface_t::add_interface(wl_registry *registry, std::uint32_t id, const char *interface, std::uint32_t version) {
  BOOST_LOG(debug) << "Available interface: "sv << interface << '(' << id << ") version "sv << version;

  if(!std::strcmp(interface, wl_output_interface.name)) {
    BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
    monitors.emplace_back(
      std::make_unique<monitor_t>(
        (wl_output *)wl_registry_bind(registry, id, &wl_output_interface, version)));
  }
  else if(!std::strcmp(interface, zxdg_output_manager_v1_interface.name)) {
    BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
    output_manager = (zxdg_output_manager_v1 *)wl_registry_bind(registry, id, &zxdg_output_manager_v1_interface, version);

    this->interface[XDG_OUTPUT] = true;
  }
  else if(!std::strcmp(interface, zwlr_export_dmabuf_manager_v1_interface.name)) {
    BOOST_LOG(info) << "Found interface: "sv << interface << '(' << id << ") version "sv << version;
    dmabuf_manager = (zwlr_export_dmabuf_manager_v1 *)wl_registry_bind(registry, id, &zwlr_export_dmabuf_manager_v1_interface, version);

    this->interface[WLR_EXPORT_DMABUF] = true;
  }
}

void interface_t::del_interface(wl_registry *registry, uint32_t id) {
  BOOST_LOG(info) << "Delete: "sv << id;
}

dmabuf_t::dmabuf_t()
    : status { READY }, frames {}, current_frame { &frames[0] }, listener {
        (decltype(zwlr_export_dmabuf_frame_v1_listener::frame))&dmabuf_t::frame,
        (decltype(zwlr_export_dmabuf_frame_v1_listener::object))&dmabuf_t::object,
        (decltype(zwlr_export_dmabuf_frame_v1_listener::ready))&dmabuf_t::ready,
        (decltype(zwlr_export_dmabuf_frame_v1_listener::cancel))&dmabuf_t::cancel,
      } {
}

void dmabuf_t::listen(zwlr_export_dmabuf_manager_v1 *dmabuf_manager, wl_output *output, bool blend_cursor) {
  auto frame = zwlr_export_dmabuf_manager_v1_capture_output(dmabuf_manager, blend_cursor, output);
  zwlr_export_dmabuf_frame_v1_add_listener(frame, &listener, this);

  status = WAITING;
}

dmabuf_t::~dmabuf_t() {
  for(auto &frame : frames) {
    frame.destroy();
  }
}

void dmabuf_t::frame(
  zwlr_export_dmabuf_frame_v1 *frame,
  std::uint32_t width, std::uint32_t height,
  std::uint32_t x, std::uint32_t y,
  std::uint32_t buffer_flags, std::uint32_t flags,
  std::uint32_t format,
  std::uint32_t high, std::uint32_t low,
  std::uint32_t obj_count) {
  auto next_frame = get_next_frame();

  next_frame->sd.fourcc   = format;
  next_frame->sd.width    = width;
  next_frame->sd.height   = height;
  next_frame->sd.modifier = (((std::uint64_t)high) << 32) | low;
}

void dmabuf_t::object(
  zwlr_export_dmabuf_frame_v1 *frame,
  std::uint32_t index,
  std::int32_t fd,
  std::uint32_t size,
  std::uint32_t offset,
  std::uint32_t stride,
  std::uint32_t plane_index) {
  auto next_frame = get_next_frame();

  next_frame->sd.fds[plane_index]     = fd;
  next_frame->sd.pitches[plane_index] = stride;
  next_frame->sd.offsets[plane_index] = offset;
}

void dmabuf_t::ready(
  zwlr_export_dmabuf_frame_v1 *frame,
  std::uint32_t tv_sec_hi, std::uint32_t tv_sec_lo, std::uint32_t tv_nsec) {

  zwlr_export_dmabuf_frame_v1_destroy(frame);

  current_frame->destroy();
  current_frame = get_next_frame();

  status = READY;
}

void dmabuf_t::cancel(
  zwlr_export_dmabuf_frame_v1 *frame,
  zwlr_export_dmabuf_frame_v1_cancel_reason reason) {

  zwlr_export_dmabuf_frame_v1_destroy(frame);

  auto next_frame = get_next_frame();
  next_frame->destroy();

  status = REINIT;
}

void frame_t::destroy() {
  for(auto x = 0; x < 4; ++x) {
    if(sd.fds[x] >= 0) {
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

  if(display.init(display_name)) {
    return {};
  }

  interface_t interface;
  interface.listen(display.registry());

  display.roundtrip();

  if(!interface[interface_t::XDG_OUTPUT]) {
    BOOST_LOG(error) << "Missing Wayland wire XDG_OUTPUT"sv;
    return {};
  }

  for(auto &monitor : interface.monitors) {
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

} // namespace wl

#pragma GCC diagnostic pop