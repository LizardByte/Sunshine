//
// Created by loki on 6/20/19.
//

extern "C" {
#include <moonlight-common-c/src/Input.h>
}

#include <iostream>

#include "input.h"
#include "utility.h"

namespace input {
using namespace std::literals;

void print(PNV_MOUSE_MOVE_PACKET packet) {
  std::cout << "--begin mouse move packet--"sv << std::endl;

  std::cout << "deltaX ["sv << util::endian::big(packet->deltaX) << ']' << std::endl;
  std::cout << "deltaY ["sv << util::endian::big(packet->deltaY) << ']' << std::endl;

  std::cout << "--end mouse move packet--"sv  << std::endl;
}

void print(PNV_MOUSE_BUTTON_PACKET packet) {
  std::cout << "--begin mouse button packet--"sv << std::endl;

  std::cout << "action ["sv << util::hex(packet->action).to_string_view() << ']' << std::endl;
  std::cout << "button ["sv << util::hex(packet->button).to_string_view() << ']' << std::endl;

  std::cout << "--end mouse button packet--"sv  << std::endl;
}

void print(PNV_SCROLL_PACKET packet) {
  std::cout << "--begin mouse scroll packet--"sv << std::endl;

  std::cout << "scrollAmt1 ["sv << util::endian::big(packet->scrollAmt1) << ']' << std::endl;

  std::cout << "--end mouse scroll packet--"sv  << std::endl;
}

void print(PNV_KEYBOARD_PACKET packet) {
  std::cout << "--begin keyboard packet--"sv << std::endl;

  std::cout << "keyAction ["sv << util::hex(packet->keyAction).to_string_view() << ']' << std::endl;
  std::cout << "keyCode ["sv << util::hex(packet->keyCode).to_string_view() << ']' << std::endl;
  std::cout << "modifiers ["sv << util::hex(packet->modifiers).to_string_view() << ']' << std::endl;

  std::cout << "--end keyboard packet--"sv  << std::endl;
}

void print(PNV_MULTI_CONTROLLER_PACKET packet) {
  std::cout << "--begin controller packet--"sv << std::endl;

  std::cout << "controllerNumber ["sv << packet->controllerNumber << ']' << std::endl;
  std::cout << "activeGamepadMask ["sv << util::hex(packet->activeGamepadMask).to_string_view() << ']' << std::endl;
  std::cout << "buttonFlags ["sv << util::hex(packet->buttonFlags).to_string_view() << ']' << std::endl;
  std::cout << "leftTrigger ["sv << util::hex(packet->leftTrigger).to_string_view() << ']' << std::endl;
  std::cout << "rightTrigger ["sv << util::hex(packet->rightTrigger).to_string_view() << ']' << std::endl;
  std::cout << "leftStickX ["sv << packet->leftStickX << ']' << std::endl;
  std::cout << "leftStickY ["sv << packet->leftStickY << ']' << std::endl;
  std::cout << "rightStickX ["sv << packet->rightStickX << ']' << std::endl;
  std::cout << "rightStickY ["sv << packet->rightStickY << ']' << std::endl;

  std::cout << "--end controller packet--"sv << std::endl;
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

void passthrough(platf::display_t::element_type *display, PNV_MOUSE_MOVE_PACKET packet) {
  platf::move_mouse(display, util::endian::big(packet->deltaX), util::endian::big(packet->deltaY));
}

void passthrough(platf::display_t::element_type *display, PNV_MOUSE_BUTTON_PACKET packet) {
  auto constexpr BUTTON_RELEASED = 0x09;

  platf::button_mouse(display, util::endian::big(packet->button), packet->action == BUTTON_RELEASED);
}

void passthrough(platf::display_t::element_type *display, PNV_KEYBOARD_PACKET packet) {
  auto constexpr BUTTON_RELEASED = 0x04;

  platf::keyboard(display, packet->keyCode & 0x00FF, packet->keyAction == BUTTON_RELEASED);
}

void passthrough(platf::display_t::element_type *display, PNV_SCROLL_PACKET packet) {
  platf::scroll(display, util::endian::big(packet->scrollAmt1));
}

void passthrough(platf::display_t::element_type *display, void *input) {
  int input_type = util::endian::big(*(int*)input);

  switch(input_type) {
    case PACKET_TYPE_MOUSE_MOVE:
      passthrough(display, (PNV_MOUSE_MOVE_PACKET)input);
      break;
    case PACKET_TYPE_MOUSE_BUTTON:
      passthrough(display, (PNV_MOUSE_BUTTON_PACKET)input);
      break;
    case PACKET_TYPE_SCROLL_OR_KEYBOARD:
    {
      char *tmp_input = (char*)input + 4;
      if(tmp_input[0] == 0x0A) {
        passthrough(display, (PNV_SCROLL_PACKET)input);
      }
      else {
        passthrough(display, (PNV_KEYBOARD_PACKET)input);
      }

      break;
    }
  }
}
}
