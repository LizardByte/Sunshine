//
// Created by loki on 6/20/19.
//

extern "C" {
#include <moonlight-common-c/src/Input.h>
}

#include <cstring>

#include "main.h"
#include "config.h"
#include "input.h"
#include "utility.h"

namespace input {
using namespace std::literals;

constexpr std::uint16_t DPAD_UP      = 0x0001;
constexpr std::uint16_t DPAD_DOWN    = 0x0002;
constexpr std::uint16_t DPAD_LEFT    = 0x0004;
constexpr std::uint16_t DPAD_RIGHT   = 0x0008;
constexpr std::uint16_t START        = 0x0010;
constexpr std::uint16_t BACK         = 0x0020;
constexpr std::uint16_t LEFT_STICK   = 0x0040;
constexpr std::uint16_t RIGHT_STICK  = 0x0080;
constexpr std::uint16_t LEFT_BUTTON  = 0x0100;
constexpr std::uint16_t RIGHT_BUTTON = 0x0200;
constexpr std::uint16_t HOME         = 0x0400;
constexpr std::uint16_t A            = 0x1000;
constexpr std::uint16_t B            = 0x2000;
constexpr std::uint16_t X            = 0x4000;
constexpr std::uint16_t Y            = 0x8000;

void print(PNV_MOUSE_MOVE_PACKET packet) {
  BOOST_LOG(debug)
    << "--begin mouse move packet--"sv << std::endl
    << "deltaX ["sv << util::endian::big(packet->deltaX) << ']' << std::endl
    << "deltaY ["sv << util::endian::big(packet->deltaY) << ']' << std::endl
    << "--end mouse move packet--"sv;
}

void print(PNV_MOUSE_BUTTON_PACKET packet) {
  BOOST_LOG(debug)
    << "--begin mouse button packet--"sv << std::endl
    << "action ["sv << util::hex(packet->action).to_string_view() << ']' << std::endl
    << "button ["sv << util::hex(packet->button).to_string_view() << ']' << std::endl
    << "--end mouse button packet--"sv;
}

void print(PNV_SCROLL_PACKET packet) {
  BOOST_LOG(debug)
    << "--begin mouse scroll packet--"sv << std::endl
    << "scrollAmt1 ["sv << util::endian::big(packet->scrollAmt1) << ']' << std::endl
    << "--end mouse scroll packet--"sv;
}

void print(PNV_KEYBOARD_PACKET packet) {
  BOOST_LOG(debug)
    << "--begin keyboard packet--"sv << std::endl
    << "keyAction ["sv << util::hex(packet->keyAction).to_string_view() << ']' << std::endl
    << "keyCode ["sv << util::hex(packet->keyCode).to_string_view() << ']' << std::endl
    << "modifiers ["sv << util::hex(packet->modifiers).to_string_view() << ']' << std::endl
    << "--end keyboard packet--"sv;
}

void print(PNV_MULTI_CONTROLLER_PACKET packet) {
  BOOST_LOG(debug)
    << "--begin controller packet--"sv << std::endl
    << "controllerNumber ["sv << packet->controllerNumber << ']' << std::endl
    << "activeGamepadMask ["sv << util::hex(packet->activeGamepadMask).to_string_view() << ']' << std::endl
    << "buttonFlags ["sv << util::hex(packet->buttonFlags).to_string_view() << ']' << std::endl
    << "leftTrigger ["sv << util::hex(packet->leftTrigger).to_string_view() << ']' << std::endl
    << "rightTrigger ["sv << util::hex(packet->rightTrigger).to_string_view() << ']' << std::endl
    << "leftStickX ["sv << packet->leftStickX << ']' << std::endl
    << "leftStickY ["sv << packet->leftStickY << ']' << std::endl
    << "rightStickX ["sv << packet->rightStickX << ']' << std::endl
    << "rightStickY ["sv << packet->rightStickY << ']' << std::endl
    << "--end controller packet--"sv;
}

constexpr int PACKET_TYPE_SCROLL_OR_KEYBOARD = PACKET_TYPE_SCROLL;
void print(void *input) {
  int input_type = util::endian::big(*(int*)input);

  switch(input_type) {
    case PACKET_TYPE_MOUSE_MOVE:
      print((PNV_MOUSE_MOVE_PACKET)input);
      break;
    case PACKET_TYPE_MOUSE_BUTTON:
      print((PNV_MOUSE_BUTTON_PACKET)input);
      break;
    case PACKET_TYPE_SCROLL_OR_KEYBOARD:
    {
      char *tmp_input = (char*)input + 4;
      if(tmp_input[0] == 0x0A) {
        print((PNV_SCROLL_PACKET)input);
      }
      else {
        print((PNV_KEYBOARD_PACKET)input);
      }

      break;
    }
    case PACKET_TYPE_MULTI_CONTROLLER:
      print((PNV_MULTI_CONTROLLER_PACKET)input);
      break;
  }
}

void passthrough(platf::input_t &input, PNV_MOUSE_MOVE_PACKET packet) {
  display_cursor = true;

  platf::move_mouse(input, util::endian::big(packet->deltaX), util::endian::big(packet->deltaY));
}

void passthrough(std::shared_ptr<input_t> &input, PNV_MOUSE_BUTTON_PACKET packet) {
  auto constexpr BUTTON_RELEASED = 0x09;

  display_cursor = true;

  auto button = util::endian::big(packet->button);
  if(button > 0 && button < input->mouse_press.size()) {
    input->mouse_press[button] = packet->action != BUTTON_RELEASED;
  }

  platf::button_mouse(input->input, button, packet->action == BUTTON_RELEASED);
}

void passthrough(std::shared_ptr<input_t> &input, PNV_KEYBOARD_PACKET packet) {
  auto constexpr BUTTON_RELEASED = 0x04;

  input->key_press[packet->keyCode] = packet->keyAction != BUTTON_RELEASED;
  platf::keyboard(input->input, packet->keyCode & 0x00FF, packet->keyAction == BUTTON_RELEASED);
}

void passthrough(platf::input_t &input, PNV_SCROLL_PACKET packet) {
  display_cursor = true;

  platf::scroll(input, util::endian::big(packet->scrollAmt1));
}

void passthrough(std::shared_ptr<input_t> &input, PNV_MULTI_CONTROLLER_PACKET packet) {
  display_cursor = false;

  std::uint16_t bf;
  std::memcpy(&bf, &packet->buttonFlags, sizeof(std::uint16_t));

  gamepad_state_t gamepad_state {
    bf,
    packet->leftTrigger,
    packet->rightTrigger,
    packet->leftStickX,
    packet->leftStickY,
    packet->rightStickX,
    packet->rightStickY
  };

  bf = gamepad_state.buttonFlags ^ input->gamepad_state.buttonFlags;
  auto bf_new = gamepad_state.buttonFlags;

  if(bf) {
    // up pressed == -1, down pressed == 1, else 0
    if((DPAD_UP | DPAD_DOWN) & bf) {
      int val = bf_new & DPAD_UP ? -1 : (bf_new & DPAD_DOWN ? 1 : 0);

      platf::gp::dpad_y(input->input, val);
    }

    if((DPAD_LEFT | DPAD_RIGHT) & bf) {
      int val = bf_new & DPAD_LEFT ? -1 : (bf_new & DPAD_RIGHT ? 1 : 0);
      platf::gp::dpad_x(input->input, val);
    }

    if(START & bf)        platf::gp::start(input->input,        bf_new & START        ? 1 : 0);
    if(LEFT_STICK & bf)   platf::gp::left_stick(input->input,   bf_new & LEFT_STICK   ? 1 : 0);
    if(RIGHT_STICK & bf)  platf::gp::right_stick(input->input,  bf_new & RIGHT_STICK  ? 1 : 0);
    if(LEFT_BUTTON & bf)  platf::gp::left_button(input->input,  bf_new & LEFT_BUTTON  ? 1 : 0);
    if(RIGHT_BUTTON & bf) platf::gp::right_button(input->input, bf_new & RIGHT_BUTTON ? 1 : 0);
    if(HOME & bf)         platf::gp::home(input->input,         bf_new & HOME         ? 1 : 0);
    if(A & bf)            platf::gp::a(input->input,            bf_new & A            ? 1 : 0);
    if(B & bf)            platf::gp::b(input->input,            bf_new & B            ? 1 : 0);
    if(X & bf)            platf::gp::x(input->input,            bf_new & X            ? 1 : 0);
    if(Y & bf)            platf::gp::y(input->input,            bf_new & Y            ? 1 : 0);

    if(BACK & bf) {
      if(BACK & bf_new) {
        platf::gp::back(input->input,1);
        input->back_timeout_id = task_pool.pushDelayed([input]() {
          platf::gp::back(input->input, 0);

          platf::gp::home(input->input,1);
          platf::gp::home(input->input,0);

          input->back_timeout_id = nullptr;
        }, config::input.back_button_timeout).task_id;
      }
      else if(input->back_timeout_id) {
        platf::gp::back(input->input, 0);

        task_pool.cancel(input->back_timeout_id);
        input->back_timeout_id = nullptr;
      }
    }
  }

  if(input->gamepad_state.lt != gamepad_state.lt) {
    platf::gp::left_trigger(input->input, gamepad_state.lt);
  }

  if(input->gamepad_state.rt != gamepad_state.rt) {
    platf::gp::right_trigger(input->input, gamepad_state.rt);
  }

  if(input->gamepad_state.lsX != gamepad_state.lsX) {
    platf::gp::left_stick_x(input->input, gamepad_state.lsX);
  }

  if(input->gamepad_state.lsY != gamepad_state.lsY) {
    platf::gp::left_stick_y(input->input, gamepad_state.lsY);
  }

  if(input->gamepad_state.rsX != gamepad_state.rsX) {
    platf::gp::right_stick_x(input->input, gamepad_state.rsX);
  }

  if(input->gamepad_state.rsY != gamepad_state.rsY) {
    platf::gp::right_stick_y(input->input, gamepad_state.rsY);
  }

  input->gamepad_state = gamepad_state;
  platf::gp::sync(input->input);
}

void passthrough_helper(std::shared_ptr<input_t> input, std::vector<std::uint8_t> &&input_data) {
  void *payload = input_data.data();

  int input_type = util::endian::big(*(int*)payload);

  switch(input_type) {
    case PACKET_TYPE_MOUSE_MOVE:
      passthrough(input->input, (PNV_MOUSE_MOVE_PACKET)payload);
      break;
    case PACKET_TYPE_MOUSE_BUTTON:
      passthrough(input, (PNV_MOUSE_BUTTON_PACKET)payload);
      break;
    case PACKET_TYPE_SCROLL_OR_KEYBOARD:
    {
      char *tmp_input = (char*)payload + 4;
      if(tmp_input[0] == 0x0A) {
        passthrough(input->input, (PNV_SCROLL_PACKET)payload);
      }
      else {
        passthrough(input, (PNV_KEYBOARD_PACKET)payload);
      }

      break;
    }
    case PACKET_TYPE_MULTI_CONTROLLER:
      passthrough(input, (PNV_MULTI_CONTROLLER_PACKET)payload);
      break;
  }
}

void reset_helper(std::shared_ptr<input_t> input) {
  for(auto &[key_press, key_down] : input->key_press) {
    if(key_down) {
      key_down = false;
      platf::keyboard(input->input, key_press & 0x00FF, true);
    }
  }

  auto &mouse_press = input->mouse_press;
  for(int x = 0; x < mouse_press.size(); ++x) {
    if(mouse_press[x]) {
      mouse_press[x] = false;

      platf::button_mouse(input->input, x + 1, true);
    }
  }
  
  NV_MULTI_CONTROLLER_PACKET fake_packet;
  fake_packet.buttonFlags = 0;
  fake_packet.leftStickX = 0;
  fake_packet.leftStickY = 0;
  fake_packet.rightStickX = 0;
  fake_packet.rightStickY = 0;
  fake_packet.leftTrigger = 0;
  fake_packet.rightTrigger = 0;

  passthrough(input, &fake_packet);
}

void passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&input_data) {
  task_pool.push(passthrough_helper, input, util::cmove(input_data));
}

void reset(std::shared_ptr<input_t> &input) {
  task_pool.push(reset_helper, input);
}

input_t::input_t() : gamepad_state {}, mouse_press {}, back_timeout_id { nullptr }, input { platf::input() } {}
}
