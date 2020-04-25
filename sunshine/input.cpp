//
// Created by loki on 6/20/19.
//

extern "C" {
#include <moonlight-common-c/src/Input.h>
}

#include <bitset>

#include "main.h"
#include "config.h"
#include "utility.h"
#include "platform/common.h"
#include "thread_pool.h"

namespace input {

constexpr auto MAX_GAMEPADS = std::min((std::size_t)platf::MAX_GAMEPADS, sizeof(std::int16_t)*8);
enum class button_state_e {
  NONE,
  DOWN,
  UP
};

template<std::size_t N>
int alloc_id(std::bitset<N> &gamepad_mask) {
  for(int x = 0; x < gamepad_mask.size(); ++x) {
    if(!gamepad_mask[x]) {
      gamepad_mask[x] = true;
      return x;
    }
  }

  return -1;
}

template<std::size_t N>
void free_id(std::bitset<N> &gamepad_mask, int id) {
  gamepad_mask[id] = false;
}

static util::TaskPool::task_id_t task_id {};
static std::unordered_map<short, bool> key_press {};
static std::array<std::uint8_t, 5> mouse_press {};

static platf::input_t platf_input;
static std::bitset<platf::MAX_GAMEPADS> gamepadMask {};

void free_gamepad(platf::input_t &platf_input, int id) {
  platf::gamepad(platf_input, id, platf::gamepad_state_t{});
  platf::free_gamepad(platf_input, id);

  free_id(gamepadMask, id);
}
struct gamepad_t {
  gamepad_t() : gamepad_state {}, back_timeout_id {}, id { -1 }, back_button_state { button_state_e::NONE } {}
  ~gamepad_t() {
    if(id >= 0) {
      task_pool.push([id=this->id]() {
        free_gamepad(platf_input, id);
      });
    }
  }

  platf::gamepad_state_t gamepad_state;

  util::ThreadPool::task_id_t back_timeout_id;

  int id;

  // When emulating the HOME button, we may need to artificially release the back button.
  // Afterwards, the gamepad state on sunshine won't match the state on Moonlight.
  // To prevent Sunshine from sending erronious input data to the active application,
  // Sunshine forces the button to be in a specific state until the gamepad state matches that of
  // Moonlight once more.
  button_state_e back_button_state;
};

struct input_t {
  input_t() : active_gamepad_state {}, gamepads (MAX_GAMEPADS) { }

  std::uint16_t active_gamepad_state;
  std::vector<gamepad_t> gamepads;
};

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
  if(button > 0 && button < mouse_press.size()) {
    mouse_press[button] = packet->action != BUTTON_RELEASED;
  }

  platf::button_mouse(platf_input, button, packet->action == BUTTON_RELEASED);
}

void repeat_key(short key_code) {
  // If key no longer pressed, stop repeating
  if(!key_press[key_code]) {
    task_id = nullptr;
    return;
  }

  platf::keyboard(platf_input, key_code & 0x00FF, false);

  task_id = task_pool.pushDelayed(repeat_key, config::input.key_repeat_period, key_code).task_id;
}

void passthrough(std::shared_ptr<input_t> &input, PNV_KEYBOARD_PACKET packet) {
  auto constexpr BUTTON_RELEASED = 0x04;

  auto release = packet->keyAction == BUTTON_RELEASED;

  auto &pressed = key_press[packet->keyCode];
  if(!pressed) {
    if(!release) {
      if(task_id) {
        task_pool.cancel(task_id);
      }

      if(config::input.key_repeat_delay.count() > 0) {
        task_id = task_pool.pushDelayed(repeat_key, config::input.key_repeat_delay, packet->keyCode).task_id;
      }
    }
    else {
      // Already released
      return;
    }
  }
  else if(!release) {
    // Already pressed down key
    return;
  }

  pressed = !release;
  platf::keyboard(platf_input, packet->keyCode & 0x00FF, release);
}

void passthrough(platf::input_t &input, PNV_SCROLL_PACKET packet) {
  display_cursor = true;

  platf::scroll(input, util::endian::big(packet->scrollAmt1));
}

int updateGamepads(std::vector<gamepad_t> &gamepads, std::int16_t old_state, std::int16_t new_state) {
  auto xorGamepadMask = old_state ^ new_state;
  if (!xorGamepadMask) {
    return 0;
  }

  for(int x = 0; x < sizeof(std::int16_t) * 8; ++x) {
    if((xorGamepadMask >> x) & 1) {
      auto &gamepad = gamepads[x];

      if((old_state >> x) & 1) {
        if (gamepad.id < 0) {
          return -1;
        }

        free_gamepad(platf_input, gamepad.id);
        gamepad.id = -1;
      }
      else {
        auto id = alloc_id(gamepadMask);

        if(id < 0) {
          // Out of gamepads
          return -1;
        }

        if(platf::alloc_gamepad(platf_input, id)) {
          free_id(gamepadMask, id);
          // allocating a gamepad failed: solution: ignore gamepads
          // The implementations of platf::alloc_gamepad already has logging
          return -1;
        }

        gamepad.id = id;
      }
    }
  }

  return 0;
}

void passthrough(std::shared_ptr<input_t> &input, PNV_MULTI_CONTROLLER_PACKET packet) {
  if(updateGamepads(input->gamepads, input->active_gamepad_state, packet->activeGamepadMask)) {
    return;
  }

  input->active_gamepad_state = packet->activeGamepadMask;

  if(packet->controllerNumber < 0 || packet->controllerNumber >= input->gamepads.size()) {
    BOOST_LOG(warning) << "ControllerNumber out of range ["sv << packet->controllerNumber << ']';

    return;
  }

  if(!((input->active_gamepad_state >> packet->controllerNumber) & 1)) {
    BOOST_LOG(warning) << "ControllerNumber ["sv << packet->controllerNumber << "] not allocated"sv;

    return;
  }

  auto &gamepad = input->gamepads[packet->controllerNumber];

  // If this gamepad has not been initialized, ignore it.
  // This could happen when platf::alloc_gamepad fails
  if(gamepad.id < 0) {
    return;
  }

  display_cursor = false;

  std::uint16_t bf = packet->buttonFlags;
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
          platf::gamepad(platf_input, gamepad.id, state);

          // Press Home button
          state.buttonFlags |= platf::HOME;
          platf::gamepad(platf_input, gamepad.id, state);

          // Release Home button
          state.buttonFlags &= ~platf::HOME;
          platf::gamepad(platf_input, gamepad.id, state);

          gamepad.back_timeout_id = nullptr;
        }, config::input.back_button_timeout).task_id;
      }
    }
    else if (gamepad.back_timeout_id) {
      task_pool.cancel(gamepad.back_timeout_id);
      gamepad.back_timeout_id = nullptr;
    }
  }

  platf::gamepad(platf_input, gamepad.id, gamepad_state);

  gamepad.gamepad_state = gamepad_state;
}

void passthrough_helper(std::shared_ptr<input_t> input, std::vector<std::uint8_t> &&input_data) {
  void *payload = input_data.data();

  int input_type = util::endian::big(*(int*)payload);

  switch(input_type) {
    case PACKET_TYPE_MOUSE_MOVE:
      passthrough(platf_input, (PNV_MOUSE_MOVE_PACKET)payload);
      break;
    case PACKET_TYPE_MOUSE_BUTTON:
      passthrough(input, (PNV_MOUSE_BUTTON_PACKET)payload);
      break;
    case PACKET_TYPE_SCROLL_OR_KEYBOARD:
    {
      char *tmp_input = (char*)payload + 4;
      if(tmp_input[0] == 0x0A) {
        passthrough(platf_input, (PNV_SCROLL_PACKET)payload);
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

void passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&input_data) {
  task_pool.push(passthrough_helper, input, util::cmove(input_data));
}

void init() {
  platf_input = platf::input();
}

std::shared_ptr<input_t> alloc() {
  auto input = std::make_shared<input_t>();

  // Workaround to ensure new frames will be captured when a client connects
  task_pool.pushDelayed([]() {
    platf::move_mouse(platf_input, 1, 1);
    platf::move_mouse(platf_input, -1, -1);
  }, 100ms);

  return input;
}
}
