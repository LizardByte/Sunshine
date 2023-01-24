#include <windows.h>

#include <cmath>

#include <ViGEm/Client.h>

#include "misc.h"
#include "src/config.h"
#include "src/main.h"
#include "src/platform/common.h"

namespace platf {
using namespace std::literals;

thread_local HDESK _lastKnownInputDesktop = nullptr;

constexpr touch_port_t target_touch_port {
  0, 0,
  65535, 65535
};

using client_t = util::safe_ptr<_VIGEM_CLIENT_T, vigem_free>;
using target_t = util::safe_ptr<_VIGEM_TARGET_T, vigem_target_free>;

static VIGEM_TARGET_TYPE map(const std::string_view &gp) {
  if(gp == "x360"sv) {
    return Xbox360Wired;
  }

  return DualShock4Wired;
}

void CALLBACK x360_notify(
  client_t::pointer client,
  target_t::pointer target,
  std::uint8_t largeMotor, std::uint8_t smallMotor,
  std::uint8_t /* led_number */,
  void *userdata);

void CALLBACK ds4_notify(
  client_t::pointer client,
  target_t::pointer target,
  std::uint8_t largeMotor, std::uint8_t smallMotor,
  DS4_LIGHTBAR_COLOR /* led_color */,
  void *userdata);

class vigem_t {
public:
  int init() {
    VIGEM_ERROR status;

    client.reset(vigem_alloc());

    status = vigem_connect(client.get());
    if(!VIGEM_SUCCESS(status)) {
      BOOST_LOG(warning) << "Couldn't setup connection to ViGEm for gamepad support ["sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    gamepads.resize(MAX_GAMEPADS);

    return 0;
  }

  int alloc_gamepad_interal(int nr, rumble_queue_t &rumble_queue, VIGEM_TARGET_TYPE gp_type) {
    auto &[rumble, gp] = gamepads[nr];
    assert(!gp);

    if(gp_type == Xbox360Wired) {
      gp.reset(vigem_target_x360_alloc());
    }
    else {
      gp.reset(vigem_target_ds4_alloc());
    }

    auto status = vigem_target_add(client.get(), gp.get());
    if(!VIGEM_SUCCESS(status)) {
      BOOST_LOG(error) << "Couldn't add Gamepad to ViGEm connection ["sv << util::hex(status).to_string_view() << ']';

      return -1;
    }

    rumble = std::move(rumble_queue);

    if(gp_type == Xbox360Wired) {
      status = vigem_target_x360_register_notification(client.get(), gp.get(), x360_notify, this);
    }
    else {
      status = vigem_target_ds4_register_notification(client.get(), gp.get(), ds4_notify, this);
    }

    if(!VIGEM_SUCCESS(status)) {
      BOOST_LOG(warning) << "Couldn't register notifications for rumble support ["sv << util::hex(status).to_string_view() << ']';
    }

    return 0;
  }

  void free_target(int nr) {
    auto &[_, gp] = gamepads[nr];

    if(gp && vigem_target_is_attached(gp.get())) {
      auto status = vigem_target_remove(client.get(), gp.get());
      if(!VIGEM_SUCCESS(status)) {
        BOOST_LOG(warning) << "Couldn't detach gamepad from ViGEm ["sv << util::hex(status).to_string_view() << ']';
      }
    }

    gp.reset();
  }

  void rumble(target_t::pointer target, std::uint8_t smallMotor, std::uint8_t largeMotor) {
    for(int x = 0; x < gamepads.size(); ++x) {
      auto &[rumble_queue, gp] = gamepads[x];

      if(gp.get() == target) {
        rumble_queue->raise(x, ((std::uint16_t)smallMotor) << 8, ((std::uint16_t)largeMotor) << 8);

        return;
      }
    }
  }

  ~vigem_t() {
    if(client) {
      for(auto &[_, gp] : gamepads) {
        if(gp && vigem_target_is_attached(gp.get())) {
          auto status = vigem_target_remove(client.get(), gp.get());
          if(!VIGEM_SUCCESS(status)) {
            BOOST_LOG(warning) << "Couldn't detach gamepad from ViGEm ["sv << util::hex(status).to_string_view() << ']';
          }
        }
      }

      vigem_disconnect(client.get());
    }
  }

  std::vector<std::pair<rumble_queue_t, target_t>> gamepads;

  client_t client;
};

void CALLBACK x360_notify(
  client_t::pointer client,
  target_t::pointer target,
  std::uint8_t largeMotor, std::uint8_t smallMotor,
  std::uint8_t /* led_number */,
  void *userdata) {

  BOOST_LOG(debug)
    << "largeMotor: "sv << (int)largeMotor << std::endl
    << "smallMotor: "sv << (int)smallMotor;

  task_pool.push(&vigem_t::rumble, (vigem_t *)userdata, target, smallMotor, largeMotor);
}

void CALLBACK ds4_notify(
  client_t::pointer client,
  target_t::pointer target,
  std::uint8_t largeMotor, std::uint8_t smallMotor,
  DS4_LIGHTBAR_COLOR /* led_color */,
  void *userdata) {

  BOOST_LOG(debug)
    << "largeMotor: "sv << (int)largeMotor << std::endl
    << "smallMotor: "sv << (int)smallMotor;

  task_pool.push(&vigem_t::rumble, (vigem_t *)userdata, target, smallMotor, largeMotor);
}

struct input_raw_t {
  ~input_raw_t() {
    delete vigem;
  }

  vigem_t *vigem;
  HKL keyboard_layout;
};

input_t input() {
  input_t result { new input_raw_t {} };
  auto &raw = *(input_raw_t *)result.get();

  raw.vigem = new vigem_t {};
  if(raw.vigem->init()) {
    delete raw.vigem;
    raw.vigem = nullptr;
  }

  // Moonlight currently sends keys normalized to the US English layout.
  // We need to use that layout when converting to scancodes.
  raw.keyboard_layout = LoadKeyboardLayoutA("00000409", 0);
  if(!raw.keyboard_layout || LOWORD(raw.keyboard_layout) != 0x409) {
    BOOST_LOG(warning) << "Unable to load US English keyboard layout for scancode translation. Keyboard input may not work in games."sv;
    raw.keyboard_layout = NULL;
  }

  return result;
}

void send_input(INPUT &i) {
retry:
  auto send = SendInput(1, &i, sizeof(INPUT));
  if(send != 1) {
    auto hDesk = syncThreadDesktop();
    if(_lastKnownInputDesktop != hDesk) {
      _lastKnownInputDesktop = hDesk;
      goto retry;
    }
    BOOST_LOG(error) << "Couldn't send input"sv;
  }
}

void abs_mouse(input_t &input, const touch_port_t &touch_port, float x, float y) {
  INPUT i {};

  i.type   = INPUT_MOUSE;
  auto &mi = i.mi;

  mi.dwFlags =
    MOUSEEVENTF_MOVE |
    MOUSEEVENTF_ABSOLUTE |

    // MOUSEEVENTF_VIRTUALDESK maps to the entirety of the desktop rather than the primary desktop
    MOUSEEVENTF_VIRTUALDESK;

  auto scaled_x = std::lround((x + touch_port.offset_x) * ((float)target_touch_port.width / (float)touch_port.width));
  auto scaled_y = std::lround((y + touch_port.offset_y) * ((float)target_touch_port.height / (float)touch_port.height));

  mi.dx = scaled_x;
  mi.dy = scaled_y;

  send_input(i);
}

void move_mouse(input_t &input, int deltaX, int deltaY) {
  INPUT i {};

  i.type   = INPUT_MOUSE;
  auto &mi = i.mi;

  mi.dwFlags = MOUSEEVENTF_MOVE;
  mi.dx      = deltaX;
  mi.dy      = deltaY;

  send_input(i);
}

void button_mouse(input_t &input, int button, bool release) {
  constexpr auto KEY_STATE_DOWN = (SHORT)0x8000;

  INPUT i {};

  i.type   = INPUT_MOUSE;
  auto &mi = i.mi;

  int mouse_button;
  if(button == 1) {
    mi.dwFlags   = release ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
    mouse_button = VK_LBUTTON;
  }
  else if(button == 2) {
    mi.dwFlags   = release ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN;
    mouse_button = VK_MBUTTON;
  }
  else if(button == 3) {
    mi.dwFlags   = release ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN;
    mouse_button = VK_RBUTTON;
  }
  else if(button == 4) {
    mi.dwFlags   = release ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
    mi.mouseData = XBUTTON1;
    mouse_button = VK_XBUTTON1;
  }
  else {
    mi.dwFlags   = release ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
    mi.mouseData = XBUTTON2;
    mouse_button = VK_XBUTTON2;
  }

  auto key_state      = GetAsyncKeyState(mouse_button);
  bool key_state_down = (key_state & KEY_STATE_DOWN) != 0;
  if(key_state_down != release) {
    BOOST_LOG(warning) << "Button state of mouse_button ["sv << button << "] does not match the desired state"sv;

    return;
  }

  send_input(i);
}

void scroll(input_t &input, int distance) {
  INPUT i {};

  i.type   = INPUT_MOUSE;
  auto &mi = i.mi;

  mi.dwFlags   = MOUSEEVENTF_WHEEL;
  mi.mouseData = distance;

  send_input(i);
}

void hscroll(input_t &input, int distance) {
  INPUT i {};

  i.type   = INPUT_MOUSE;
  auto &mi = i.mi;

  mi.dwFlags   = MOUSEEVENTF_HWHEEL;
  mi.mouseData = distance;

  send_input(i);
}

void keyboard(input_t &input, uint16_t modcode, bool release) {
  auto raw = (input_raw_t *)input.get();

  INPUT i {};
  i.type   = INPUT_KEYBOARD;
  auto &ki = i.ki;

  // For some reason, MapVirtualKey(VK_LWIN, MAPVK_VK_TO_VSC) doesn't seem to work :/
  if(modcode != VK_LWIN && modcode != VK_RWIN && modcode != VK_PAUSE && raw->keyboard_layout != NULL) {
    ki.wScan = MapVirtualKeyEx(modcode, MAPVK_VK_TO_VSC, raw->keyboard_layout);
  }

  // If we can map this to a scancode, send it as a scancode for maximum game compatibility.
  if(ki.wScan) {
    ki.dwFlags = KEYEVENTF_SCANCODE;
  }
  else {
    // If there is no scancode mapping, send it as a regular VK event.
    ki.wVk = modcode;
  }

  // https://docs.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input#keystroke-message-flags
  switch(modcode) {
  case VK_RMENU:
  case VK_RCONTROL:
  case VK_INSERT:
  case VK_DELETE:
  case VK_HOME:
  case VK_END:
  case VK_PRIOR:
  case VK_NEXT:
  case VK_UP:
  case VK_DOWN:
  case VK_LEFT:
  case VK_RIGHT:
  case VK_DIVIDE:
    ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    break;
  default:
    break;
  }

  if(release) {
    ki.dwFlags |= KEYEVENTF_KEYUP;
  }

  send_input(i);
}

void unicode(input_t &input, char *utf8, int size) {
  // We can do no worse than one UTF-16 character per byte of UTF-8
  WCHAR wide[size];

  int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, size, wide, size);
  if(chars <= 0) {
    return;
  }

  // Send all key down events
  for(int i = 0; i < chars; i++) {
    INPUT input {};
    input.type       = INPUT_KEYBOARD;
    input.ki.wScan   = wide[i];
    input.ki.dwFlags = KEYEVENTF_UNICODE;
    send_input(input);
  }

  // Send all key up events
  for(int i = 0; i < chars; i++) {
    INPUT input {};
    input.type       = INPUT_KEYBOARD;
    input.ki.wScan   = wide[i];
    input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    send_input(input);
  }
}

int alloc_gamepad(input_t &input, int nr, rumble_queue_t rumble_queue) {
  auto raw = (input_raw_t *)input.get();

  if(!raw->vigem) {
    return 0;
  }

  return raw->vigem->alloc_gamepad_interal(nr, rumble_queue, map(config::input.gamepad));
}

void free_gamepad(input_t &input, int nr) {
  auto raw = (input_raw_t *)input.get();

  if(!raw->vigem) {
    return;
  }

  raw->vigem->free_target(nr);
}

static VIGEM_ERROR x360_update(client_t::pointer client, target_t::pointer gp, const gamepad_state_t &gamepad_state) {
  auto &xusb = *(PXUSB_REPORT)&gamepad_state;

  return vigem_target_x360_update(client, gp, xusb);
}

static DS4_DPAD_DIRECTIONS ds4_dpad(const gamepad_state_t &gamepad_state) {
  auto flags = gamepad_state.buttonFlags;
  if(flags & DPAD_UP) {
    if(flags & DPAD_RIGHT) {
      return DS4_BUTTON_DPAD_NORTHEAST;
    }
    else if(flags & DPAD_LEFT) {
      return DS4_BUTTON_DPAD_NORTHWEST;
    }
    else {
      return DS4_BUTTON_DPAD_NORTH;
    }
  }

  else if(flags & DPAD_DOWN) {
    if(flags & DPAD_RIGHT) {
      return DS4_BUTTON_DPAD_SOUTHEAST;
    }
    else if(flags & DPAD_LEFT) {
      return DS4_BUTTON_DPAD_SOUTHWEST;
    }
    else {
      return DS4_BUTTON_DPAD_SOUTH;
    }
  }

  else if(flags & DPAD_RIGHT) {
    return DS4_BUTTON_DPAD_EAST;
  }

  else if(flags & DPAD_LEFT) {
    return DS4_BUTTON_DPAD_WEST;
  }

  return DS4_BUTTON_DPAD_NONE;
}

static DS4_BUTTONS ds4_buttons(const gamepad_state_t &gamepad_state) {
  int buttons {};

  auto flags = gamepad_state.buttonFlags;
  // clang-format off
  if(flags & LEFT_STICK)   buttons |= DS4_BUTTON_THUMB_LEFT;
  if(flags & RIGHT_STICK)  buttons |= DS4_BUTTON_THUMB_RIGHT;
  if(flags & LEFT_BUTTON)  buttons |= DS4_BUTTON_SHOULDER_LEFT;
  if(flags & RIGHT_BUTTON) buttons |= DS4_BUTTON_SHOULDER_RIGHT;
  if(flags & START)        buttons |= DS4_BUTTON_OPTIONS;
  if(flags & A)            buttons |= DS4_BUTTON_CROSS;
  if(flags & B)            buttons |= DS4_BUTTON_CIRCLE;
  if(flags & X)            buttons |= DS4_BUTTON_SQUARE;
  if(flags & Y)            buttons |= DS4_BUTTON_TRIANGLE;

  if(gamepad_state.lt > 0) buttons |= DS4_BUTTON_TRIGGER_LEFT;
  if(gamepad_state.rt > 0) buttons |= DS4_BUTTON_TRIGGER_RIGHT;
  // clang-format on

  return (DS4_BUTTONS)buttons;
}

static DS4_SPECIAL_BUTTONS ds4_special_buttons(const gamepad_state_t &gamepad_state) {
  int buttons {};

  if(gamepad_state.buttonFlags & BACK) buttons |= DS4_SPECIAL_BUTTON_TOUCHPAD;
  if(gamepad_state.buttonFlags & HOME) buttons |= DS4_SPECIAL_BUTTON_PS;

  return (DS4_SPECIAL_BUTTONS)buttons;
}

static std::uint8_t to_ds4_triggerX(std::int16_t v) {
  return (v + std::numeric_limits<std::uint16_t>::max() / 2 + 1) / 257;
}

static std::uint8_t to_ds4_triggerY(std::int16_t v) {
  auto new_v = -((std::numeric_limits<std::uint16_t>::max() / 2 + v - 1)) / 257;

  return new_v == 0 ? 0xFF : (std::uint8_t)new_v;
}

static VIGEM_ERROR ds4_update(client_t::pointer client, target_t::pointer gp, const gamepad_state_t &gamepad_state) {
  DS4_REPORT report;

  DS4_REPORT_INIT(&report);
  DS4_SET_DPAD(&report, ds4_dpad(gamepad_state));
  report.wButtons |= ds4_buttons(gamepad_state);
  report.bSpecial = ds4_special_buttons(gamepad_state);

  report.bTriggerL = gamepad_state.lt;
  report.bTriggerR = gamepad_state.rt;

  report.bThumbLX = to_ds4_triggerX(gamepad_state.lsX);
  report.bThumbLY = to_ds4_triggerY(gamepad_state.lsY);

  report.bThumbRX = to_ds4_triggerX(gamepad_state.rsX);
  report.bThumbRY = to_ds4_triggerY(gamepad_state.rsY);

  return vigem_target_ds4_update(client, gp, report);
}


void gamepad(input_t &input, int nr, const gamepad_state_t &gamepad_state) {
  auto vigem = ((input_raw_t *)input.get())->vigem;

  // If there is no gamepad support
  if(!vigem) {
    return;
  }

  auto &[_, gp] = vigem->gamepads[nr];

  VIGEM_ERROR status;

  if(vigem_target_get_type(gp.get()) == Xbox360Wired) {
    status = x360_update(vigem->client.get(), gp.get(), gamepad_state);
  }
  else {
    status = ds4_update(vigem->client.get(), gp.get(), gamepad_state);
  }

  if(!VIGEM_SUCCESS(status)) {
    BOOST_LOG(warning) << "Couldn't send gamepad input to ViGEm ["sv << util::hex(status).to_string_view() << ']';
  }
}

void freeInput(void *p) {
  auto input = (input_raw_t *)p;

  delete input;
}

std::vector<std::string_view> &supported_gamepads() {
  // ds4 == ps4
  static std::vector<std::string_view> gps {
    "x360"sv, "ds4"sv, "ps4"sv
  };

  return gps;
}
} // namespace platf
