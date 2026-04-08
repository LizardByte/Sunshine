/**
 * @file src/platform/linux/input/wl_mouse.h
 * @brief Declarations for wlr-virtual-pointer mouse input handling.
 */
#pragma once

#ifdef SUNSHINE_BUILD_WAYLAND

// lib includes
#include <wayland-client.h>

// forward-declare generated protocol types so the header compiles without
// including the full generated header (which is only available at build time)
struct zwlr_virtual_pointer_manager_v1;
struct zwlr_virtual_pointer_v1;

namespace platf::wl_mouse {

  /**
   * Holds the Wayland connection and virtual pointer handles for the
   * wlr-virtual-pointer-unstable-v1 protocol.
   *
   * One instance lives inside input_raw_t, initialized only when
   * config::input.wlr_virtual_mouse is true.
   */
  struct state_t {
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    zwlr_virtual_pointer_manager_v1 *manager = nullptr;
    zwlr_virtual_pointer_v1 *pointer = nullptr;

    /**
     * Connect to WAYLAND_DISPLAY, discover zwlr_virtual_pointer_manager_v1
     * via the registry, and create a virtual pointer device.
     * @return true on success, false if the compositor does not support the protocol.
     */
    bool init();

    /** Release all Wayland resources and disconnect. */
    void destroy();
  };

  void move(state_t *state, int deltaX, int deltaY);
  void move_abs(state_t *state, float x, float y, uint32_t width, uint32_t height);
  void button(state_t *state, int button, bool release);
  void scroll(state_t *state, int high_res_distance);
  void hscroll(state_t *state, int high_res_distance);

}  // namespace platf::wl_mouse

#endif  // SUNSHINE_BUILD_WAYLAND
