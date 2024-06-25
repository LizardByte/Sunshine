/**
 * @file src/platform/linux/input/inputtino_pen.cpp
 * @brief Definitions for inputtino pen input handling.
 */
#include <boost/locale.hpp>
#include <inputtino/input.hpp>
#include <libevdev/libevdev.h>

#include "src/config.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"

#include "inputtino_common.h"
#include "inputtino_pen.h"

using namespace std::literals;

namespace platf::pen {
  void
  update(client_input_raw_t *raw, const touch_port_t &touch_port, const pen_input_t &pen) {
    if (raw->pen) {
      // First set the buttons
      (*raw->pen).set_btn(inputtino::PenTablet::PRIMARY, pen.penButtons & LI_PEN_BUTTON_PRIMARY);
      (*raw->pen).set_btn(inputtino::PenTablet::SECONDARY, pen.penButtons & LI_PEN_BUTTON_SECONDARY);
      (*raw->pen).set_btn(inputtino::PenTablet::TERTIARY, pen.penButtons & LI_PEN_BUTTON_TERTIARY);

      // Set the tool
      inputtino::PenTablet::TOOL_TYPE tool;
      switch (pen.toolType) {
        case LI_TOOL_TYPE_PEN:
          tool = inputtino::PenTablet::PEN;
          break;
        case LI_TOOL_TYPE_ERASER:
          tool = inputtino::PenTablet::ERASER;
          break;
        default:
          tool = inputtino::PenTablet::SAME_AS_BEFORE;
          break;
      }

      // Normalize rotation value to 0-359 degree range
      auto rotation = pen.rotation;
      if (rotation != LI_ROT_UNKNOWN) {
        rotation %= 360;
      }

      // Here we receive:
      //  - Rotation: degrees from vertical in Y dimension (parallel to screen, 0..360)
      //  - Tilt: degrees from vertical in Z dimension (perpendicular to screen, 0..90)
      float tilt_x = 0;
      float tilt_y = 0;
      // Convert polar coordinates into Y tilt angles
      if (pen.tilt != LI_TILT_UNKNOWN && rotation != LI_ROT_UNKNOWN) {
        auto rotation_rads = deg2rad(rotation);
        auto tilt_rads = deg2rad(pen.tilt);
        auto r = std::sin(tilt_rads);
        auto z = std::cos(tilt_rads);

        tilt_x = std::atan2(std::sin(-rotation_rads) * r, z) * 180.f / M_PI;
        tilt_y = std::atan2(std::cos(-rotation_rads) * r, z) * 180.f / M_PI;
      }

      bool is_touching = pen.eventType == LI_TOUCH_EVENT_DOWN || pen.eventType == LI_TOUCH_EVENT_MOVE;

      (*raw->pen).place_tool(tool,
        pen.x,
        pen.y,
        is_touching ? pen.pressureOrDistance : -1,
        is_touching ? -1 : pen.pressureOrDistance,
        tilt_x,
        tilt_y);
    }
  }
}  // namespace platf::pen
