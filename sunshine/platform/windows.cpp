#include <thread>

#include <windows.h>
#include <winuser.h>

#include "sunshine/main.h"
#include "common.h"

namespace platf {
using namespace std::literals;
std::string get_local_ip() { return "192.168.0.119"s; }

input_t input() {
  return nullptr;
}

void move_mouse(input_t &input, int deltaX, int deltaY) {
  INPUT i {};

  i.type = INPUT_MOUSE;
  auto &mi = i.mi;

  mi.dwFlags = MOUSEEVENTF_MOVE;
  mi.dx = deltaX;
  mi.dy = deltaY;

  auto send = SendInput(1, &i, sizeof(INPUT));
  if(send != 1) {
    BOOST_LOG(warning) << "Couldn't send mouse movement input"sv;
  }
}

void button_mouse(input_t &input, int button, bool release) {
  constexpr SHORT KEY_STATE_DOWN = 0x8000;

  INPUT i {};

  i.type = INPUT_MOUSE;
  auto &mi = i.mi;

  int mouse_button;
  if(button == 1) {
    mi.dwFlags = release ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
    mouse_button = VK_LBUTTON;
  }
  else if(button == 2) {
    mi.dwFlags = release ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN;
    mouse_button = VK_MBUTTON;
  }
  else if(button == 3) {
    mi.dwFlags = release ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN;
    mouse_button = VK_RBUTTON;
  }
  else if(button == 4) {
    mi.dwFlags = release ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
    mi.mouseData = XBUTTON1;
    mouse_button = VK_XBUTTON1;
  }
  else {
    mi.dwFlags = release ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
    mi.mouseData = XBUTTON2;
    mouse_button = VK_XBUTTON2;
  }

  auto key_state = GetAsyncKeyState(mouse_button);
  bool key_state_down = (key_state & KEY_STATE_DOWN) != 0;
  if(key_state_down != release) {
    BOOST_LOG(warning) << "Button state of mouse_button ["sv << button << "] does not match the desired state"sv;

    return;
  }

  auto send = SendInput(1, &i, sizeof(INPUT));
  if(send != 1) {
    BOOST_LOG(warning) << "Couldn't send mouse button input"sv;
  }
}

void scroll(input_t &input, int distance) {
  INPUT i {};

  i.type = INPUT_MOUSE;
  auto &mi = i.mi;

  mi.dwFlags = MOUSEEVENTF_WHEEL;
  mi.mouseData = distance;

  auto send = SendInput(1, &i, sizeof(INPUT));
  if(send != 1) {
    BOOST_LOG(warning) << "Couldn't send moue movement input"sv;
  }
}

void keyboard(input_t &input, uint16_t modcode, bool release) {
  constexpr SHORT KEY_STATE_DOWN = 0x8000;

  if(modcode == VK_RMENU) {
    modcode = VK_LWIN;
  }

  auto key_state = GetAsyncKeyState(modcode);
  bool key_state_down = (key_state & KEY_STATE_DOWN) != 0;
  if(key_state_down != release) {
    BOOST_LOG(warning) << "Key state of vkey ["sv << util::hex(modcode).to_string_view() << "] does not match the desired state ["sv << (release ? "on]"sv : "off]"sv);

    return;
  }

  INPUT i {};
  i.type = INPUT_KEYBOARD;
  auto &ki = i.ki;

  // For some reason, MapVirtualKey(VK_LWIN, MAPVK_VK_TO_VSC) doesn't seem to work :/
  if(modcode != VK_LWIN && modcode != VK_RWIN) {
    ki.wScan = MapVirtualKey(modcode, MAPVK_VK_TO_VSC);
    ki.dwFlags = KEYEVENTF_SCANCODE;
  }
  else {
    ki.wVk = modcode;
  }

  if(release) {
    ki.dwFlags |= KEYEVENTF_KEYUP;
  }

  auto send = SendInput(1, &i, sizeof(INPUT));
  if(send != 1) {
    BOOST_LOG(warning) << "Couldn't send moue movement input"sv;
  }
}

namespace gp {
void dpad_y(input_t &input, int button_state) {} // up pressed == -1, down pressed == 1, else 0
void dpad_x(input_t &input, int button_state) {} // left pressed == -1, right pressed == 1, else 0
void start(input_t &input, int button_down) {}
void back(input_t &input, int button_down) {}
void left_stick(input_t &input, int button_down) {}
void right_stick(input_t &input, int button_down) {}
void left_button(input_t &input, int button_down) {}
void right_button(input_t &input, int button_down) {}
void home(input_t &input, int button_down) {}
void a(input_t &input, int button_down) {}
void b(input_t &input, int button_down) {}
void x(input_t &input, int button_down) {}
void y(input_t &input, int button_down) {}
void left_trigger(input_t &input, std::uint8_t abs_z) {}
void right_trigger(input_t &input, std::uint8_t abs_z) {}
void left_stick_x(input_t &input, std::int16_t x) {}
void left_stick_y(input_t &input, std::int16_t y) {}
void right_stick_x(input_t &input, std::int16_t x) {}
void right_stick_y(input_t &input, std::int16_t y) {}
void sync(input_t &input) {}
}

void freeInput(void*) {}
}
