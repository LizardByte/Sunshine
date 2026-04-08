/**
 * @file src/platform/linux/input/wl_mouse.cpp
 * @brief Definitions for wlr-virtual-pointer mouse input handling.
 *
 * Implements mouse movement, button, and scroll injection using the
 * wlr-virtual-pointer-unstable-v1 Wayland protocol.  Only compiled when
 * SUNSHINE_BUILD_WAYLAND is defined (i.e. Wayland dev libraries are present).
 */

#ifdef SUNSHINE_BUILD_WAYLAND

// standard includes
#include <ctime>

// lib includes
#include <wayland-client.h>

// generated protocol bindings (produced by wayland-scanner at cmake configure time)
#include <wlr-virtual-pointer-unstable-v1.h>

// local includes
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "wl_mouse.h"

namespace platf::wl_mouse {

  namespace {

    /**
     * Return a monotonic timestamp in milliseconds, as required by Wayland
     * input event timestamps.
     */
    uint32_t
    now_ms() {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (uint32_t) (ts.tv_sec * 1000 + ts.tv_nsec / 1'000'000);
    }

    // -------------------------------------------------------------------------
    // Registry listener — binds zwlr_virtual_pointer_manager_v1 when found
    // -------------------------------------------------------------------------

    void
    registry_global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
      auto *state = reinterpret_cast<state_t *>(data);
      if (strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
        uint32_t bind_version = std::min(version, (uint32_t) 2);
        state->manager = reinterpret_cast<zwlr_virtual_pointer_manager_v1 *>(
          wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, bind_version));
      }
    }

    void
    registry_global_remove(void * /*data*/, wl_registry * /*registry*/, uint32_t /*name*/) {
      // nothing to do
    }

    constexpr wl_registry_listener registry_listener = {
      .global = registry_global,
      .global_remove = registry_global_remove,
    };

  }  // anonymous namespace

  // ---------------------------------------------------------------------------
  // state_t lifecycle
  // ---------------------------------------------------------------------------

  bool
  state_t::init() {
    display = wl_display_connect(nullptr);
    if (!display) {
      BOOST_LOG(error) << "wl_mouse: failed to connect to Wayland display (is WAYLAND_DISPLAY set?)";
      return false;
    }

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, this);
    wl_display_roundtrip(display);  // discover globals

    if (!manager) {
      BOOST_LOG(error) << "wl_mouse: compositor does not support zwlr_virtual_pointer_manager_v1; "
                       << "ensure you are running a wlroots-based compositor (sway, Hyprland, labwc, …)";
      destroy();
      return false;
    }

    // NULL seat = compositor picks the default seat
    pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, nullptr);
    wl_display_roundtrip(display);  // finish device creation

    BOOST_LOG(info) << "wl_mouse: virtual pointer created via wlr-virtual-pointer protocol";
    return true;
  }

  void
  state_t::destroy() {
    if (pointer) {
      zwlr_virtual_pointer_v1_destroy(pointer);
      pointer = nullptr;
    }
    if (manager) {
      zwlr_virtual_pointer_manager_v1_destroy(manager);
      manager = nullptr;
    }
    if (registry) {
      wl_registry_destroy(registry);
      registry = nullptr;
    }
    if (display) {
      wl_display_disconnect(display);
      display = nullptr;
    }
  }

  // ---------------------------------------------------------------------------
  // Mouse operations
  // ---------------------------------------------------------------------------

  void
  move(state_t *state, int deltaX, int deltaY) {
    zwlr_virtual_pointer_v1_motion(state->pointer, now_ms(),
      wl_fixed_from_int(deltaX), wl_fixed_from_int(deltaY));
    zwlr_virtual_pointer_v1_frame(state->pointer);
    wl_display_flush(state->display);
  }

  void
  move_abs(state_t *state, float x, float y, uint32_t width, uint32_t height) {
    // Protocol expects unsigned fixed-point coordinates in the range [0, extent)
    zwlr_virtual_pointer_v1_motion_absolute(state->pointer, now_ms(),
      (uint32_t) x, (uint32_t) y, width, height);
    zwlr_virtual_pointer_v1_frame(state->pointer);
    wl_display_flush(state->display);
  }

  void
  button(state_t *state, int btn, bool release) {
    // Map Sunshine button constants to Linux evdev BTN_* codes
    uint32_t code;
    switch (btn) {
      case BUTTON_LEFT:
        code = 0x110;  // BTN_LEFT
        break;
      case BUTTON_RIGHT:
        code = 0x111;  // BTN_RIGHT
        break;
      case BUTTON_MIDDLE:
        code = 0x112;  // BTN_MIDDLE
        break;
      case BUTTON_X1:
        code = 0x113;  // BTN_SIDE
        break;
      case BUTTON_X2:
        code = 0x114;  // BTN_EXTRA
        break;
      default:
        BOOST_LOG(warning) << "wl_mouse: unknown button: " << btn;
        return;
    }

    uint32_t state_val = release ? WL_POINTER_BUTTON_STATE_RELEASED : WL_POINTER_BUTTON_STATE_PRESSED;
    zwlr_virtual_pointer_v1_button(state->pointer, now_ms(), code, state_val);
    zwlr_virtual_pointer_v1_frame(state->pointer);
    wl_display_flush(state->display);
  }

  void
  scroll(state_t *state, int high_res_distance) {
    // Sunshine uses 120 units per detent (same as Windows HID).
    // Convert to a pixel-like wl_fixed_t value; 15 px per detent is a common desktop default.
    wl_fixed_t value = wl_fixed_from_double(high_res_distance / 120.0 * 15.0);
    int32_t discrete = high_res_distance / 120;  // whole detents

    zwlr_virtual_pointer_v1_axis_source(state->pointer, WL_POINTER_AXIS_SOURCE_WHEEL);
    zwlr_virtual_pointer_v1_axis(state->pointer, now_ms(), WL_POINTER_AXIS_VERTICAL_SCROLL, value);
    if (discrete != 0) {
      zwlr_virtual_pointer_v1_axis_discrete(state->pointer, now_ms(),
        WL_POINTER_AXIS_VERTICAL_SCROLL, value, discrete);
    }
    zwlr_virtual_pointer_v1_frame(state->pointer);
    wl_display_flush(state->display);
  }

  void
  hscroll(state_t *state, int high_res_distance) {
    wl_fixed_t value = wl_fixed_from_double(high_res_distance / 120.0 * 15.0);
    int32_t discrete = high_res_distance / 120;

    zwlr_virtual_pointer_v1_axis_source(state->pointer, WL_POINTER_AXIS_SOURCE_WHEEL);
    zwlr_virtual_pointer_v1_axis(state->pointer, now_ms(), WL_POINTER_AXIS_HORIZONTAL_SCROLL, value);
    if (discrete != 0) {
      zwlr_virtual_pointer_v1_axis_discrete(state->pointer, now_ms(),
        WL_POINTER_AXIS_HORIZONTAL_SCROLL, value, discrete);
    }
    zwlr_virtual_pointer_v1_frame(state->pointer);
    wl_display_flush(state->display);
  }

}  // namespace platf::wl_mouse

#endif  // SUNSHINE_BUILD_WAYLAND
