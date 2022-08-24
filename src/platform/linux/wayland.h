#ifndef SUNSHINE_WAYLAND_H
#define SUNSHINE_WAYLAND_H

#include <bitset>

#ifdef SUNSHINE_BUILD_WAYLAND
#include <wlr-export-dmabuf-unstable-v1.h>
#include <xdg-output-unstable-v1.h>
#endif

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
  egl::surface_descriptor_t sd;

  void destroy();
};

class dmabuf_t {
public:
  enum status_e {
    WAITING,
    READY,
    REINIT,
  };

  dmabuf_t(dmabuf_t &&)      = delete;
  dmabuf_t(const dmabuf_t &) = delete;

  dmabuf_t &operator=(const dmabuf_t &) = delete;
  dmabuf_t &operator=(dmabuf_t &&) = delete;

  dmabuf_t();

  void listen(zwlr_export_dmabuf_manager_v1 *dmabuf_manager, wl_output *output, bool blend_cursor = false);

  ~dmabuf_t();

  void frame(
    zwlr_export_dmabuf_frame_v1 *frame,
    std::uint32_t width, std::uint32_t height,
    std::uint32_t x, std::uint32_t y,
    std::uint32_t buffer_flags, std::uint32_t flags,
    std::uint32_t format,
    std::uint32_t high, std::uint32_t low,
    std::uint32_t obj_count);

  void object(
    zwlr_export_dmabuf_frame_v1 *frame,
    std::uint32_t index,
    std::int32_t fd,
    std::uint32_t size,
    std::uint32_t offset,
    std::uint32_t stride,
    std::uint32_t plane_index);

  void ready(
    zwlr_export_dmabuf_frame_v1 *frame,
    std::uint32_t tv_sec_hi, std::uint32_t tv_sec_lo, std::uint32_t tv_nsec);

  void cancel(
    zwlr_export_dmabuf_frame_v1 *frame,
    zwlr_export_dmabuf_frame_v1_cancel_reason reason);

  inline frame_t *get_next_frame() {
    return current_frame == &frames[0] ? &frames[1] : &frames[0];
  }

  status_e status;

  std::array<frame_t, 2> frames;
  frame_t *current_frame;

  zwlr_export_dmabuf_frame_v1_listener listener;
};

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
  enum interface_e {
    XDG_OUTPUT,
    WLR_EXPORT_DMABUF,
    MAX_INTERFACES,
  };

  interface_t(interface_t &&)      = delete;
  interface_t(const interface_t &) = delete;

  interface_t &operator=(const interface_t &) = delete;
  interface_t &operator=(interface_t &&) = delete;

  interface_t() noexcept;

  void listen(wl_registry *registry);

  std::vector<std::unique_ptr<monitor_t>> monitors;

  zwlr_export_dmabuf_manager_v1 *dmabuf_manager;
  zxdg_output_manager_v1 *output_manager;

  bool operator[](interface_e bit) const {
    return interface[bit];
  }

private:
  void add_interface(wl_registry *registry, std::uint32_t id, const char *interface, std::uint32_t version);
  void del_interface(wl_registry *registry, uint32_t id);

  std::bitset<MAX_INTERFACES> interface;

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

  inline display_internal_t::pointer get() {
    return display_internal.get();
  }

private:
  display_internal_t display_internal;
};

std::vector<std::unique_ptr<monitor_t>> monitors(const char *display_name = nullptr);

int init();
} // namespace wl
#else

struct wl_output;
struct zxdg_output_manager_v1;

namespace wl {
class monitor_t {
public:
  monitor_t(monitor_t &&)      = delete;
  monitor_t(const monitor_t &) = delete;

  monitor_t &operator=(const monitor_t &) = delete;
  monitor_t &operator=(monitor_t &&) = delete;

  monitor_t(wl_output *output);

  void listen(zxdg_output_manager_v1 *output_manager);

  wl_output *output;

  std::string name;
  std::string description;

  platf::touch_port_t viewport;
};

inline std::vector<std::unique_ptr<monitor_t>> monitors(const char *display_name = nullptr) { return {}; }

inline int init() { return -1; }
} // namespace wl
#endif

#endif