//
// Created by loki on 6/20/19.
//

#ifndef SUNSHINE_INPUT_H
#define SUNSHINE_INPUT_H

#include "platform/common.h"

namespace input {
struct gamepad_state_t {
  std::uint16_t buttonFlags;
  std::uint8_t lt;
  std::uint8_t rt;
  std::int16_t lsX;
  std::int16_t lsY;
  std::int16_t rsX;
  std::int16_t rsY;
};

struct input_t {
  input_t();

  gamepad_state_t gamepad_state;
  platf::input_t input;
};

void print(void *input);
void passthrough(input_t &, void *input);
}

#endif //SUNSHINE_INPUT_H
