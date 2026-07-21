/**
 * @file src/platform/linux/input/virtualhid.cpp
 * @brief Definitions for libvirtualhid Unix input handling.
 */

// standard includes
#include <memory>
#include <utility>

// platform includes
#ifdef SUNSHINE_BUILD_X11
  #include <X11/Xlib.h>
#endif

// local includes
#include "src/platform/virtualhid_input.h"

using namespace std::literals;

namespace platf {
  platform_caps::caps_t get_capabilities() {
    platform_caps::caps_t caps = 0;
    const auto runtime = virtualhid::create_runtime();
    if (!runtime) {
      return caps;
    }

    if (const auto &capabilities = runtime->capabilities(); config::input.native_pen_touch && (capabilities.supports_touchscreen || capabilities.supports_pen_tablet)) {
      caps |= platform_caps::pen_touch;
    }
    if (virtualhid::configured_gamepad_supports_touchpad()) {
      caps |= platform_caps::controller_touch;
    }

    return caps;
  }

  std::optional<util::point_t> get_mouse_loc(input_t & /*input*/) {
#ifdef SUNSHINE_BUILD_X11
    auto *display = XOpenDisplay(nullptr);
    if (!display) {
      return std::nullopt;
    }

    const auto root = DefaultRootWindow(display);
    Window root_return {};
    Window child_return {};
    int root_x = 0;
    int root_y = 0;
    int window_x = 0;
    int window_y = 0;
    unsigned int mask = 0;
    const auto queried = XQueryPointer(display, root, &root_return, &child_return, &root_x, &root_y, &window_x, &window_y, &mask);
    XCloseDisplay(display);

    if (!queried) {
      return std::nullopt;
    }

    return util::point_t {
      static_cast<double>(root_x),
      static_cast<double>(root_y)
    };
#else
    return std::nullopt;
#endif
  }

  std::vector<supported_gamepad_t> &supported_gamepads(input_t *input) {
    static std::vector<supported_gamepad_t> gamepads;
    if (!input || !input->get()) {
      gamepads = virtualhid::static_supported_gamepads();
      return gamepads;
    }

    gamepads = virtualhid::supported_gamepads(virtualhid::get_input_context(*input).runtime.get());
    return gamepads;
  }
}  // namespace platf
