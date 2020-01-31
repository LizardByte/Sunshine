//
// Created by loki on 6/20/19.
//

#ifndef SUNSHINE_INPUT_H
#define SUNSHINE_INPUT_H

#include "platform/common.h"
#include "thread_pool.h"

namespace input {
enum class button_state_e {
  NONE,
  DOWN,
  UP
};

struct gamepad_t {
  gamepad_t();
  platf::gamepad_state_t gamepad_state;

  util::ThreadPool::task_id_t back_timeout_id;


  // When emulating the HOME button, we may need to artificially release the back button.
  // Afterwards, the gamepad state on sunshine won't match the state on Moonlight.
  // To prevent Sunshine from sending erronious input data to the active application,
  // Sunshine forces the button to be in a specific state until the gamepad state matches that of
  // Moonlight once more.
  button_state_e back_button_state;
};
struct input_t {
  input_t();

  std::unordered_map<short, bool> key_press;
  std::array<std::uint8_t, 5> mouse_press;

  platf::input_t input;

  std::uint16_t active_gamepad_state;
  std::vector<gamepad_t> gamepads;
};

void print(void *input);
void passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&input_data);
void reset(std::shared_ptr<input_t> &input);
}

#endif //SUNSHINE_INPUT_H
