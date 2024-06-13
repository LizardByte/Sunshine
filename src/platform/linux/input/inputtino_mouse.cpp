#include <boost/locale.hpp>
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"

#include "inputtino_common.h"
#include "inputtino_mouse.h"

using namespace std::literals;

namespace platf::mouse {

  void
  move(input_raw_t *raw, int deltaX, int deltaY) {
    if (raw->mouse) {
      (*raw->mouse).move(deltaX, deltaY);
    }
  }

  void
  move_abs(input_raw_t *raw, const touch_port_t &touch_port, float x, float y) {
    if (raw->mouse) {
      (*raw->mouse).move_abs(x, y, touch_port.width, touch_port.height);
    }
  }

  void
  button(input_raw_t *raw, int button, bool release) {
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
      }
      else {
        (*raw->mouse).press(btn_type);
      }
    }
  }

  void
  scroll(input_raw_t *raw, int high_res_distance) {
    if (raw->mouse) {
      (*raw->mouse).vertical_scroll(high_res_distance);
    }
  }

  void
  hscroll(input_raw_t *raw, int high_res_distance) {
    if (raw->mouse) {
      (*raw->mouse).horizontal_scroll(high_res_distance);
    }
  }

  util::point_t
  get_location(input_raw_t *raw) {
    if (raw->mouse) {
      auto TARGET_WIDTH = 1920; // TODO: change method signature and get from display
      auto TARGET_HEIGHT = 1080; // TODO: get from display
      auto li = create_libinput_context({(*raw->mouse).get_nodes()[1]});
      auto event = get_event(li);
      auto p_event = libinput_event_get_pointer_event(event.get());
      auto x = libinput_event_pointer_get_absolute_x(p_event);
      auto y = libinput_event_pointer_get_absolute_y(p_event);
      return {x, y};
    }
    return {0, 0};
  }
}  // namespace platf::mouse
