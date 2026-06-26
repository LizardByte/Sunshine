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

using namespace std::literals;

namespace platf::mouse {

  /**
   * @brief Apply a relative pointer movement to the virtual mouse.
   */
  void move(input_raw_t *raw, int deltaX, int deltaY) {
    if (raw->mouse) {
      (*raw->mouse).move(deltaX, deltaY);
    }
  }

  /**
   * @brief Move abs using the backend coordinate system.
   */
  void move_abs(input_raw_t *raw, const touch_port_t &touch_port, float x, float y) {
    if (raw->mouse) {
      (*raw->mouse).move_abs(x, y, touch_port.width, touch_port.height);
    }
  }

  /**
   * @brief Press or release a virtual mouse button.
   */
  void button(input_raw_t *raw, int button, bool release) {
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

  /**
   * @brief Apply a vertical scroll event to the virtual mouse.
   */
  void scroll(input_raw_t *raw, int high_res_distance) {
    if (raw->mouse) {
      (*raw->mouse).vertical_scroll(high_res_distance);
    }
  }

  /**
   * @brief Apply a horizontal scroll event to the virtual mouse.
   */
  void hscroll(input_raw_t *raw, int high_res_distance) {
    if (raw->mouse) {
      (*raw->mouse).horizontal_scroll(high_res_distance);
    }
  }

  /**
   * @brief Return the current virtual pointer location.
   */
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
