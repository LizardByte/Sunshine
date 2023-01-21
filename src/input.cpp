// Created by loki on 6/20/19.

// define uint32_t for <moonlight-common-c/src/Input.h>
#include <cstdint>
extern "C" {
#include <moonlight-common-c/src/Input.h>
#include <moonlight-common-c/src/Limelight.h>
}

#include <bitset>
#include <unordered_map>

#include "config.h"
#include "input.h"
#include "main.h"
#include "platform/common.h"
#include "thread_pool.h"
#include "utility.h"

using namespace std::literals;
namespace input {

constexpr auto MAX_GAMEPADS = std::min((std::size_t)platf::MAX_GAMEPADS, sizeof(std::int16_t) * 8);
#define DISABLE_LEFT_BUTTON_DELAY ((util::ThreadPool::task_id_t)0x01)
#define ENABLE_LEFT_BUTTON_DELAY nullptr

constexpr auto VKEY_SHIFT    = 0x10;
constexpr auto VKEY_LSHIFT   = 0xA0;
constexpr auto VKEY_RSHIFT   = 0xA1;
constexpr auto VKEY_CONTROL  = 0x11;
constexpr auto VKEY_LCONTROL = 0xA2;
constexpr auto VKEY_RCONTROL = 0xA3;
constexpr auto VKEY_MENU     = 0x12;
constexpr auto VKEY_LMENU    = 0xA4;
constexpr auto VKEY_RMENU    = 0xA5;

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

static util::TaskPool::task_id_t key_press_repeat_id {};
static std::unordered_map<short, bool> key_press {};
static std::array<std::uint8_t, 5> mouse_press {};

static platf::input_t platf_input;
static std::bitset<platf::MAX_GAMEPADS> gamepadMask {};

void free_gamepad(platf::input_t &platf_input, int id) {
  platf::gamepad(platf_input, id, platf::gamepad_state_t {});
  platf::free_gamepad(platf_input, id);

  free_id(gamepadMask, id);
}
struct gamepad_t {
  gamepad_t() : gamepad_state {}, back_timeout_id {}, id { -1 }, back_button_state { button_state_e::NONE } {}
  ~gamepad_t() {
    if(id >= 0) {
      task_pool.push([id = this->id]() {
        free_gamepad(platf_input, id);
      });
    }
  }

  platf::gamepad_state_t gamepad_state;

  util::ThreadPool::task_id_t back_timeout_id;

  int id;

  // When emulating the HOME button, we may need to artificially release the back button.
  // Afterwards, the gamepad state on sunshine won't match the state on Moonlight.
  // To prevent Sunshine from sending erroneous input data to the active application,
  // Sunshine forces the button to be in a specific state until the gamepad state matches that of
  // Moonlight once more.
  button_state_e back_button_state;
};

struct input_t {
  enum shortkey_e {
    CTRL  = 0x1,
    ALT   = 0x2,
    SHIFT = 0x4,

    SHORTCUT = CTRL | ALT | SHIFT
  };

  input_t(
    safe::mail_raw_t::event_t<input::touch_port_t> touch_port_event,
    platf::rumble_queue_t rumble_queue)
      : shortcutFlags {},
        active_gamepad_state {},
        gamepads(MAX_GAMEPADS),
        touch_port_event { std::move(touch_port_event) },
        rumble_queue { std::move(rumble_queue) },
        mouse_left_button_timeout {},
        touch_port { 0, 0, 0, 0, 0, 0, 1.0f } {}

  // Keep track of alt+ctrl+shift key combo
  int shortcutFlags;

  std::uint16_t active_gamepad_state;
  std::vector<gamepad_t> gamepads;

  safe::mail_raw_t::event_t<input::touch_port_t> touch_port_event;
  platf::rumble_queue_t rumble_queue;

  util::ThreadPool::task_id_t mouse_left_button_timeout;

  input::touch_port_t touch_port;
};

/**
 * Apply shortcut based on VKEY
 * On success
 *    return > 0
 * On nothing
 *    return 0
 */
inline int apply_shortcut(short keyCode) {
  constexpr auto VK_F1  = 0x70;
  constexpr auto VK_F13 = 0x7C;

  BOOST_LOG(debug) << "Apply Shortcut: 0x"sv << util::hex((std::uint8_t)keyCode).to_string_view();

  if(keyCode >= VK_F1 && keyCode <= VK_F13) {
    mail::man->event<int>(mail::switch_display)->raise(keyCode - VK_F1);
    return 1;
  }

  switch(keyCode) {
  case 0x4E /* VKEY_N */:
    display_cursor = !display_cursor;
    return 1;
  }

  return 0;
}

void print(PNV_REL_MOUSE_MOVE_PACKET packet) {
  BOOST_LOG(debug)
    << "--begin relative mouse move packet--"sv << std::endl
    << "deltaX ["sv << util::endian::big(packet->deltaX) << ']' << std::endl
    << "deltaY ["sv << util::endian::big(packet->deltaY) << ']' << std::endl
    << "--end relative mouse move packet--"sv;
}

void print(PNV_ABS_MOUSE_MOVE_PACKET packet) {
  BOOST_LOG(debug)
    << "--begin absolute mouse move packet--"sv << std::endl
    << "x      ["sv << util::endian::big(packet->x) << ']' << std::endl
    << "y      ["sv << util::endian::big(packet->y) << ']' << std::endl
    << "width  ["sv << util::endian::big(packet->width) << ']' << std::endl
    << "height ["sv << util::endian::big(packet->height) << ']' << std::endl
    << "--end absolute mouse move packet--"sv;
}

void print(PNV_MOUSE_BUTTON_PACKET packet) {
  BOOST_LOG(debug)
    << "--begin mouse button packet--"sv << std::endl
    << "action ["sv << util::hex(packet->header.magic).to_string_view() << ']' << std::endl
    << "button ["sv << util::hex(packet->button).to_string_view() << ']' << std::endl
    << "--end mouse button packet--"sv;
}

void print(PNV_SCROLL_PACKET packet) {
  BOOST_LOG(debug)
    << "--begin mouse scroll packet--"sv << std::endl
    << "scrollAmt1 ["sv << util::endian::big(packet->scrollAmt1) << ']' << std::endl
    << "--end mouse scroll packet--"sv;
}

void print(PSS_HSCROLL_PACKET packet) {
  BOOST_LOG(debug)
    << "--begin mouse hscroll packet--"sv << std::endl
    << "scrollAmount ["sv << util::endian::big(packet->scrollAmount) << ']' << std::endl
    << "--end mouse hscroll packet--"sv;
}

void print(PNV_KEYBOARD_PACKET packet) {
  BOOST_LOG(debug)
    << "--begin keyboard packet--"sv << std::endl
    << "keyAction ["sv << util::hex(packet->header.magic).to_string_view() << ']' << std::endl
    << "keyCode ["sv << util::hex(packet->keyCode).to_string_view() << ']' << std::endl
    << "modifiers ["sv << util::hex(packet->modifiers).to_string_view() << ']' << std::endl
    << "flags ["sv << util::hex(packet->flags).to_string_view() << ']' << std::endl
    << "--end keyboard packet--"sv;
}

void print(PNV_UNICODE_PACKET packet) {
  std::string text(packet->text, util::endian::big(packet->header.size) - sizeof(packet->header.magic));
  BOOST_LOG(debug)
    << "--begin unicode packet--"sv << std::endl
    << "text ["sv << text << ']' << std::endl
    << "--end unicode packet--"sv;
}

void print(PNV_MULTI_CONTROLLER_PACKET packet) {
  // Moonlight spams controller packet even when not necessary
  BOOST_LOG(verbose)
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

void print(void *payload) {
  auto header = (PNV_INPUT_HEADER)payload;

  switch(util::endian::little(header->magic)) {
  case MOUSE_MOVE_REL_MAGIC_GEN5:
    print((PNV_REL_MOUSE_MOVE_PACKET)payload);
    break;
  case MOUSE_MOVE_ABS_MAGIC:
    print((PNV_ABS_MOUSE_MOVE_PACKET)payload);
    break;
  case MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5:
  case MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5:
    print((PNV_MOUSE_BUTTON_PACKET)payload);
    break;
  case SCROLL_MAGIC_GEN5:
    print((PNV_SCROLL_PACKET)payload);
    break;
  case SS_HSCROLL_MAGIC:
    print((PSS_HSCROLL_PACKET)payload);
    break;
  case KEY_DOWN_EVENT_MAGIC:
  case KEY_UP_EVENT_MAGIC:
    print((PNV_KEYBOARD_PACKET)payload);
    break;
  case UTF8_TEXT_EVENT_MAGIC:
    print((PNV_UNICODE_PACKET)payload);
    break;
  case MULTI_CONTROLLER_MAGIC_GEN5:
    print((PNV_MULTI_CONTROLLER_PACKET)payload);
    break;
  }
}

void passthrough(std::shared_ptr<input_t> &input, PNV_REL_MOUSE_MOVE_PACKET packet) {
  input->mouse_left_button_timeout = DISABLE_LEFT_BUTTON_DELAY;
  platf::move_mouse(platf_input, util::endian::big(packet->deltaX), util::endian::big(packet->deltaY));
}

void passthrough(std::shared_ptr<input_t> &input, PNV_ABS_MOUSE_MOVE_PACKET packet) {
  if(input->mouse_left_button_timeout == DISABLE_LEFT_BUTTON_DELAY) {
    input->mouse_left_button_timeout = ENABLE_LEFT_BUTTON_DELAY;
  }

  auto &touch_port_event = input->touch_port_event;
  auto &touch_port       = input->touch_port;
  if(touch_port_event->peek()) {
    touch_port = *touch_port_event->pop();
  }

  float x = util::endian::big(packet->x);
  float y = util::endian::big(packet->y);

  // Prevent divide by zero
  // Don't expect it to happen, but just in case
  if(!packet->width || !packet->height) {
    BOOST_LOG(warning) << "Moonlight passed invalid dimensions"sv;

    return;
  }

  auto width  = (float)util::endian::big(packet->width);
  auto height = (float)util::endian::big(packet->height);

  auto scalarX = touch_port.width / width;
  auto scalarY = touch_port.height / height;

  x *= scalarX;
  y *= scalarY;

  auto offsetX = touch_port.client_offsetX;
  auto offsetY = touch_port.client_offsetY;

  std::clamp(x, offsetX, width - offsetX);
  std::clamp(y, offsetY, height - offsetY);

  platf::touch_port_t abs_port {
    touch_port.offset_x, touch_port.offset_y,
    touch_port.env_width, touch_port.env_height
  };

  platf::abs_mouse(platf_input, abs_port, (x - offsetX) * touch_port.scalar_inv, (y - offsetY) * touch_port.scalar_inv);
}

void passthrough(std::shared_ptr<input_t> &input, PNV_MOUSE_BUTTON_PACKET packet) {
  auto release = util::endian::little(packet->header.magic) == MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5;
  auto button  = util::endian::big(packet->button);
  if(button > 0 && button < mouse_press.size()) {
    if(mouse_press[button] != release) {
      // button state is already what we want
      return;
    }

    mouse_press[button] = !release;
  }
  ///////////////////////////////////
  /*/
    * When Moonlight sends mouse input through absolute coordinates,
    * it's possible that BUTTON_RIGHT is pressed down immediately after releasing BUTTON_LEFT.
    * As a result, Sunshine will left-click on hyperlinks in the browser before right-clicking
    *
    * This can be solved by delaying BUTTON_LEFT, however, any delay on input is undesirable during gaming
    * As a compromise, Sunshine will only put delays on BUTTON_LEFT when
    * absolute mouse coordinates have been sent.
    *
    * Try to make sure BUTTON_RIGHT gets called before BUTTON_LEFT is released.
    *
    * input->mouse_left_button_timeout can only be nullptr
    * when the last mouse coordinates were absolute
   /*/
  if(button == BUTTON_LEFT && release && !input->mouse_left_button_timeout) {
    auto f = [=]() {
      auto left_released = mouse_press[BUTTON_LEFT];
      if(left_released) {
        // Already released left button
        return;
      }
      platf::button_mouse(platf_input, BUTTON_LEFT, release);

      mouse_press[BUTTON_LEFT]         = false;
      input->mouse_left_button_timeout = nullptr;
    };

    input->mouse_left_button_timeout = task_pool.pushDelayed(std::move(f), 10ms).task_id;

    return;
  }
  if(
    button == BUTTON_RIGHT && !release &&
    input->mouse_left_button_timeout > DISABLE_LEFT_BUTTON_DELAY) {
    platf::button_mouse(platf_input, BUTTON_RIGHT, false);
    platf::button_mouse(platf_input, BUTTON_RIGHT, true);

    mouse_press[BUTTON_RIGHT] = false;

    return;
  }
  ///////////////////////////////////

  platf::button_mouse(platf_input, button, release);
}

short map_keycode(short keycode) {
  auto it = config::input.keybindings.find(keycode);
  if(it != std::end(config::input.keybindings)) {
    return it->second;
  }

  return keycode;
}

/**
 * Update flags for keyboard shortcut combo's
 */
inline void update_shortcutFlags(int *flags, short keyCode, bool release) {
  switch(keyCode) {
  case VKEY_SHIFT:
  case VKEY_LSHIFT:
  case VKEY_RSHIFT:
    if(release) {
      *flags &= ~input_t::SHIFT;
    }
    else {
      *flags |= input_t::SHIFT;
    }
    break;
  case VKEY_CONTROL:
  case VKEY_LCONTROL:
  case VKEY_RCONTROL:
    if(release) {
      *flags &= ~input_t::CTRL;
    }
    else {
      *flags |= input_t::CTRL;
    }
    break;
  case VKEY_MENU:
  case VKEY_LMENU:
  case VKEY_RMENU:
    if(release) {
      *flags &= ~input_t::ALT;
    }
    else {
      *flags |= input_t::ALT;
    }
    break;
  }
}

void repeat_key(short key_code) {
  // If key no longer pressed, stop repeating
  if(!key_press[key_code]) {
    key_press_repeat_id = nullptr;
    return;
  }

  platf::keyboard(platf_input, map_keycode(key_code), false);

  key_press_repeat_id = task_pool.pushDelayed(repeat_key, config::input.key_repeat_period, key_code).task_id;
}

void passthrough(std::shared_ptr<input_t> &input, PNV_KEYBOARD_PACKET packet) {
  auto release = util::endian::little(packet->header.magic) == KEY_UP_EVENT_MAGIC;
  auto keyCode = packet->keyCode & 0x00FF;

  auto &pressed = key_press[keyCode];
  if(!pressed) {
    if(!release) {
      // A new key has been pressed down, we need to check for key combo's
      // If a key-combo has been pressed down, don't pass it through
      if(input->shortcutFlags == input_t::SHORTCUT && apply_shortcut(keyCode) > 0) {
        return;
      }

      if(key_press_repeat_id) {
        task_pool.cancel(key_press_repeat_id);
      }

      if(config::input.key_repeat_delay.count() > 0) {
        key_press_repeat_id = task_pool.pushDelayed(repeat_key, config::input.key_repeat_delay, keyCode).task_id;
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

  update_shortcutFlags(&input->shortcutFlags, map_keycode(keyCode), release);
  platf::keyboard(platf_input, map_keycode(keyCode), release);
}

void passthrough(PNV_SCROLL_PACKET packet) {
  platf::scroll(platf_input, util::endian::big(packet->scrollAmt1));
}

void passthrough(PSS_HSCROLL_PACKET packet) {
  platf::hscroll(platf_input, util::endian::big(packet->scrollAmount));
}

void passthrough(PNV_UNICODE_PACKET packet) {
  auto size = util::endian::big(packet->header.size) - sizeof(packet->header.magic);
  platf::unicode(platf_input, packet->text, size);
}

int updateGamepads(std::vector<gamepad_t> &gamepads, std::int16_t old_state, std::int16_t new_state, const platf::rumble_queue_t &rumble_queue) {
  auto xorGamepadMask = old_state ^ new_state;
  if(!xorGamepadMask) {
    return 0;
  }

  for(int x = 0; x < sizeof(std::int16_t) * 8; ++x) {
    if((xorGamepadMask >> x) & 1) {
      auto &gamepad = gamepads[x];

      if((old_state >> x) & 1) {
        if(gamepad.id < 0) {
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

        if(platf::alloc_gamepad(platf_input, id, rumble_queue)) {
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
  if(updateGamepads(input->gamepads, input->active_gamepad_state, packet->activeGamepadMask, input->rumble_queue)) {
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

  std::uint16_t bf = packet->buttonFlags;
  platf::gamepad_state_t gamepad_state {
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

  bf     = gamepad_state.buttonFlags ^ gamepad.gamepad_state.buttonFlags;
  bf_new = gamepad_state.buttonFlags;

  if(platf::BACK & bf) {
    if(platf::BACK & bf_new) {
      // Don't emulate home button if timeout < 0
      if(config::input.back_button_timeout >= 0ms) {
        auto f = [input, controller = packet->controllerNumber]() {
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
        };

        gamepad.back_timeout_id = task_pool.pushDelayed(std::move(f), config::input.back_button_timeout).task_id;
      }
    }
    else if(gamepad.back_timeout_id) {
      task_pool.cancel(gamepad.back_timeout_id);
      gamepad.back_timeout_id = nullptr;
    }
  }

  platf::gamepad(platf_input, gamepad.id, gamepad_state);

  gamepad.gamepad_state = gamepad_state;
}

void passthrough_helper(std::shared_ptr<input_t> input, std::vector<std::uint8_t> &&input_data) {
  void *payload = input_data.data();
  auto header   = (PNV_INPUT_HEADER)payload;

  switch(util::endian::little(header->magic)) {
  case MOUSE_MOVE_REL_MAGIC_GEN5:
    passthrough(input, (PNV_REL_MOUSE_MOVE_PACKET)payload);
    break;
  case MOUSE_MOVE_ABS_MAGIC:
    passthrough(input, (PNV_ABS_MOUSE_MOVE_PACKET)payload);
    break;
  case MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5:
  case MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5:
    passthrough(input, (PNV_MOUSE_BUTTON_PACKET)payload);
    break;
  case SCROLL_MAGIC_GEN5:
    passthrough((PNV_SCROLL_PACKET)payload);
    break;
  case SS_HSCROLL_MAGIC:
    passthrough((PSS_HSCROLL_PACKET)payload);
    break;
  case KEY_DOWN_EVENT_MAGIC:
  case KEY_UP_EVENT_MAGIC:
    passthrough(input, (PNV_KEYBOARD_PACKET)payload);
    break;
  case UTF8_TEXT_EVENT_MAGIC:
    passthrough((PNV_UNICODE_PACKET)payload);
    break;
  case MULTI_CONTROLLER_MAGIC_GEN5:
    passthrough(input, (PNV_MULTI_CONTROLLER_PACKET)payload);
    break;
  }
}

void passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&input_data) {
  task_pool.push(passthrough_helper, input, util::cmove(input_data));
}

void reset(std::shared_ptr<input_t> &input) {
  task_pool.cancel(key_press_repeat_id);
  task_pool.cancel(input->mouse_left_button_timeout);

  // Ensure input is synchronous, by using the task_pool
  task_pool.push([]() {
    for(int x = 0; x < mouse_press.size(); ++x) {
      if(mouse_press[x]) {
        platf::button_mouse(platf_input, x, true);
        mouse_press[x] = false;
      }
    }

    for(auto &kp : key_press) {
      platf::keyboard(platf_input, kp.first & 0x00FF, true);
      key_press[kp.first] = false;
    }
  });
}

class deinit_t : public platf::deinit_t {
public:
  ~deinit_t() override {
    platf_input.reset();
  }
};

[[nodiscard]] std::unique_ptr<platf::deinit_t> init() {
  platf_input = platf::input();

  return std::make_unique<deinit_t>();
}

std::shared_ptr<input_t> alloc(safe::mail_t mail) {
  auto input = std::make_shared<input_t>(
    mail->event<input::touch_port_t>(mail::touch_port),
    mail->queue<platf::rumble_t>(mail::rumble));

  // Workaround to ensure new frames will be captured when a client connects
  task_pool.pushDelayed([]() {
    platf::move_mouse(platf_input, 1, 1);
    platf::move_mouse(platf_input, -1, -1);
  },
    100ms);

  return input;
}
} // namespace input
