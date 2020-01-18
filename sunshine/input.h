//
// Created by loki on 6/20/19.
//

#ifndef SUNSHINE_INPUT_H
#define SUNSHINE_INPUT_H

#include "platform/common.h"
#include "thread_pool.h"

namespace input {
struct input_t {
  input_t();

  platf::gamepad_state_t gamepad_state;
  std::unordered_map<short, bool> key_press;
  std::array<std::uint8_t, 3> mouse_press;

  util::ThreadPool::task_id_t back_timeout_id;

  platf::input_t input;
};

void print(void *input);
void passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&input_data);
void reset(std::shared_ptr<input_t> &input);
}

#endif //SUNSHINE_INPUT_H
