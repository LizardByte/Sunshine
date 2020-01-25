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
  if(packet->controllerNumber < 0 || packet->controllerNumber >= input->gamepads.size()) {
    BOOST_LOG(warning) << "ControllerNumber out of range ["sv << packet->controllerNumber << ']';

    return;
  }

  auto &gamepad = input->gamepads[packet->controllerNumber];

  display_cursor = false;

  std::uint16_t bf;
  std::memcpy(&bf, &packet->buttonFlags, sizeof(std::uint16_t));

  platf::gamepad_state_t gamepad_state{
    bf,
    packet->leftTrigger,
    packet->rightTrigger,
    packet->leftStickX,
    packet->leftStickY,
    packet->rightStickX,
    packet->rightStickY
  };

  auto bf_new = gamepad_state.buttonFlags;
  switch(gamepad.back_button_state) {
    case button_state_e::UP:
      if(!(platf::BACK & bf_new)) {
        gamepad.back_button_state = button_state_e::NONE;
      }
      gamepad_state.buttonFlags &= ~platf::BACK;
      break;
    case button_state_e::DOWN:
      if(platf::BACK & bf_new) {
        gamepad.back_button_state = button_state_e::NONE;
      }
      gamepad_state.buttonFlags |= platf::BACK;
      break;
    case button_state_e::NONE:
      break;
  }

  bf = gamepad_state.buttonFlags ^ gamepad.gamepad_state.buttonFlags;
  bf_new = gamepad_state.buttonFlags;

  if (platf::BACK & bf) {
    if (platf::BACK & bf_new) {

      // Don't emulate home button if timeout < 0
      if(config::input.back_button_timeout >= 0ms) {
        gamepad.back_timeout_id = task_pool.pushDelayed([input, controller=packet->controllerNumber]() {
          auto &gamepad = input->gamepads[controller];

          auto &state = gamepad.gamepad_state;

          // Force the back button up
          gamepad.back_button_state = button_state_e::UP;
          state.buttonFlags &= ~platf::BACK;
          platf::gamepad(input->input, controller, state);

          // Press Home button
          state.buttonFlags |= platf::HOME;
          platf::gamepad(input->input, controller, state);

          // Release Home button
          state.buttonFlags &= ~platf::HOME;
          platf::gamepad(input->input, controller, state);

          gamepad.back_timeout_id = nullptr;
        }, config::input.back_button_timeout).task_id;
      }
    }
    else if (gamepad.back_timeout_id) {
      task_pool.cancel(gamepad.back_timeout_id);
      gamepad.back_timeout_id = nullptr;
    }
  }

  platf::gamepad(input->input, packet->controllerNumber, gamepad_state);

  gamepad.gamepad_state = gamepad_state;
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

input_t::input_t() : mouse_press {}, input { platf::input() }, gamepads(platf::MAX_GAMEPADS) {}
gamepad_t::gamepad_t() : gamepad_state {}, back_timeout_id {}, back_button_state { button_state_e::NONE } {}
}
