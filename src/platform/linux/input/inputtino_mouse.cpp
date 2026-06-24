/**
 * @file src/platform/linux/input/inputtino_mouse.cpp
 * @brief Definitions for inputtino mouse input handling.
 */
// lib includes
#include <boost/locale.hpp>
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

// local includes
#include "inputtino_common.h"
#include "inputtino_mouse.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"
#include "wl_mouse.h"

using namespace std::literals;

namespace platf::mouse {

  void move(input_raw_t *raw, int deltaX, int deltaY) {
#ifdef SUNSHINE_BUILD_WAYLAND
    if (config::input.wlr_virtual_mouse && raw->wl_mouse.pointer) {
      platf::wl_mouse::move(&raw->wl_mouse, deltaX, deltaY);
      return;
    }
#endif
    if (raw->mouse) {
      (*raw->mouse).move(deltaX, deltaY);
    }
  }

  void move_abs(input_raw_t *raw, const touch_port_t &touch_port, float x, float y) {
#ifdef SUNSHINE_BUILD_WAYLAND
    if (config::input.wlr_virtual_mouse && raw->wl_mouse.pointer) {
      platf::wl_mouse::move_abs(&raw->wl_mouse, x, y, touch_port.width, touch_port.height);
      return;
    }
#endif
    if (raw->mouse) {
      (*raw->mouse).move_abs(x, y, touch_port.width, touch_port.height);
    }
  }

  void button(input_raw_t *raw, int button, bool release) {
#ifdef SUNSHINE_BUILD_WAYLAND
    if (config::input.wlr_virtual_mouse && raw->wl_mouse.pointer) {
      platf::wl_mouse::button(&raw->wl_mouse, button, release);
      return;
    }
#endif
    if (raw->mouse) {
      inputtino::Mouse::MOUSE_BUTTON btn_type;
      switch (button) {
        case BUTTON_LEFT:
          btn_type = inputtino::Mouse::LEFT;
          break;
        case BUTTON_MIDDLE:
          btn_type = inputtino::Mouse::MIDDLE;
          break;
        case BUTTON_RIGHT:
          btn_type = inputtino::Mouse::RIGHT;
          break;
        case BUTTON_X1:
          btn_type = inputtino::Mouse::SIDE;
          break;
        case BUTTON_X2:
          btn_type = inputtino::Mouse::EXTRA;
          break;
        default:
          BOOST_LOG(warning) << "Unknown mouse button: " << button;
          return;
      }
      if (release) {
        (*raw->mouse).release(btn_type);
      } else {
        (*raw->mouse).press(btn_type);
      }
    }
  }

  void scroll(input_raw_t *raw, int high_res_distance) {
#ifdef SUNSHINE_BUILD_WAYLAND
    if (config::input.wlr_virtual_mouse && raw->wl_mouse.pointer) {
      platf::wl_mouse::scroll(&raw->wl_mouse, high_res_distance);
      return;
    }
#endif
    if (raw->mouse) {
      (*raw->mouse).vertical_scroll(high_res_distance);
    }
  }

  void hscroll(input_raw_t *raw, int high_res_distance) {
#ifdef SUNSHINE_BUILD_WAYLAND
    if (config::input.wlr_virtual_mouse && raw->wl_mouse.pointer) {
      platf::wl_mouse::hscroll(&raw->wl_mouse, high_res_distance);
      return;
    }
#endif
    if (raw->mouse) {
      (*raw->mouse).horizontal_scroll(high_res_distance);
    }
  }

  util::point_t get_location(input_raw_t *raw) {
    if (raw->mouse) {
      // TODO: decide what to do after https://github.com/games-on-whales/inputtino/issues/6 is resolved.
      // TODO: auto x = (*raw->mouse).get_absolute_x();
      // TODO: auto y = (*raw->mouse).get_absolute_y();
      return {0, 0};
    }
    return {0, 0};
  }
}  // namespace platf::mouse
