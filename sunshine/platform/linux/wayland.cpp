#include <wayland-client.h>
#include <wayland-util.h>

#include <cstdlib>

#include "sunshine/main.h"
#include "sunshine/platform/common.h"
#include "sunshine/utility.h"
#include "wayland.h"

extern const wl_interface wl_output_interface;

using namespace std::literals;

// Disable warning for converting incompatible functions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

namespace wl {
void test() {
  display_t display;

  if(display.init()) {
    return;
  }

  interface_t interface { display.registry() };

  display.roundtrip();

  for(auto &monitor : interface.monitors) {
    monitor->listen(interface.output_manager);
  }

  display.roundtrip();
}

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

inline void monitor_t::xdg_description(zxdg_output_v1 *, const char *description) {
  this->description = description;

  BOOST_LOG(info) << "Found monitor: "sv << this->description;
}

inline void monitor_t::xdg_position(zxdg_output_v1 *, std::int32_t x, std::int32_t y) {
  viewport.offset_x = x;
  viewport.offset_y = y;

  BOOST_LOG(info) << "Offset: "sv << x << 'x' << y;
}

inline void monitor_t::xdg_size(zxdg_output_v1 *, std::int32_t width, std::int32_t height) {
  viewport.width  = width;
  viewport.height = height;

  BOOST_LOG(info) << "Resolution: "sv << width << 'x' << height;
}

inline void monitor_t::xdg_done(zxdg_output_v1 *) {
  BOOST_LOG(info) << "All info about monitor ["sv << name << "] has been send"sv;
}

inline void monitor_t::listen(zxdg_output_manager_v1 *output_manager) {
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

inline interface_t::interface_t(wl_registry *registry)
    : output_manager { nullptr }, listener {
        (decltype(wl_registry_listener::global))&interface_t::add_interface,
        (decltype(wl_registry_listener::global_remove))&interface_t::del_interface,
      } {
  wl_registry_add_listener(registry, &listener, this);
}

inline void interface_t::add_interface(wl_registry *registry, std::uint32_t id, const char *interface, std::uint32_t version) {
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
  }
}

inline void interface_t::del_interface(wl_registry *registry, uint32_t id) {
  BOOST_LOG(info) << "Delete: "sv << id;
}

} // namespace wl

#pragma GCC diagnostic pop