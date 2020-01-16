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
  INPUT i {};

  i.type = INPUT_MOUSE;
  auto &mi = i.mi;

  if(button == 1) {
    mi.dwFlags = release ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
  }
  else if(button == 2) {
    mi.dwFlags = release ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_MIDDLEDOWN;
  }
  else {
    mi.dwFlags = release ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_RIGHTDOWN;
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
  mi.mouseData = distance / 120;

  auto send = SendInput(1, &i, sizeof(INPUT));
  if(send != 1) {
    BOOST_LOG(warning) << "Couldn't send moue movement input"sv;
  }
}

void keyboard(input_t &input, uint16_t modcode, bool release) {
  if(modcode == VK_RMENU) {
    modcode = VK_LBUTTON;
  }

  INPUT i {};
  i.type = INPUT_KEYBOARD;
  auto &ki = i.ki;

//  ki.dwFlags = KEYEVENTF_SCANCODE;
  if(release) {
    ki.dwFlags = KEYEVENTF_KEYUP;
  }

  ki.wVk = modcode;

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
