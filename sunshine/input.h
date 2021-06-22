//
// Created by loki on 6/20/19.
//

#ifndef SUNSHINE_INPUT_H
#define SUNSHINE_INPUT_H

#include "platform/common.h"
#include "thread_safe.h"
namespace input {

struct input_t;

void print(void *input);
void reset(std::shared_ptr<input_t> &input);
void passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&input_data);


void init();

std::shared_ptr<input_t> alloc(safe::mail_t mail);
} // namespace input

#endif //SUNSHINE_INPUT_H
