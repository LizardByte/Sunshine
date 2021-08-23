#ifndef SUNSHINE_WAYLAND_H
#define SUNSHINE_WAYLAND_H

#include <xdg-output-unstable-v1.h>

namespace wl {
using display_internal_t = util::safe_ptr<wl_display, wl_display_disconnect>;

class monitor_t {
public:
  monitor_t(monitor_t &&)      = delete;
  monitor_t(const monitor_t &) = delete;

  monitor_t &operator=(const monitor_t &) = delete;
  monitor_t &operator=(monitor_t &&) = delete;

  monitor_t(wl_output *output);

  void xdg_name(zxdg_output_v1 *, const char *name);
  void xdg_description(zxdg_output_v1 *, const char *description);
  void xdg_position(zxdg_output_v1 *, std::int32_t x, std::int32_t y);
  void xdg_size(zxdg_output_v1 *, std::int32_t width, std::int32_t height);
  void xdg_done(zxdg_output_v1 *);

  void listen(zxdg_output_manager_v1 *output_manager);

  wl_output *output;

  std::string name;
  std::string description;

  platf::touch_port_t viewport;

  zxdg_output_v1_listener listener;
};

class interface_t {
  struct bind_t {
    std::uint32_t id;
    std::uint32_t version;
  };

public:
  interface_t(interface_t &&)      = delete;
  interface_t(const interface_t &) = delete;

  interface_t &operator=(const interface_t &) = delete;
  interface_t &operator=(interface_t &&) = delete;

  interface_t(wl_registry *registry);

  std::vector<std::unique_ptr<monitor_t>> monitors;
  zxdg_output_manager_v1 *output_manager;

private:
  void add_interface(wl_registry *registry, std::uint32_t id, const char *interface, std::uint32_t version);
  void del_interface(wl_registry *registry, uint32_t id);

  wl_registry_listener listener;
};

class display_t {
public:
  /**
   * Initialize display with display_name
   * If display_name == nullptr -> display_name = std::getenv("WAYLAND_DISPLAY")
   */
  int init(const char *display_name = nullptr);

  // Roundtrip with Wayland connection
  void roundtrip();

  // Get the registry associated with the display
  // No need to manually free the registry
  wl_registry *registry();

private:
  display_internal_t display_internal;
};

void test();
} // namespace wl

#endif