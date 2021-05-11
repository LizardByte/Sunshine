//
// Created by loki on 6/20/19.
//

#ifndef SUNSHINE_INPUT_H
#define SUNSHINE_INPUT_H

#include "thread_safe.h"
#include "platform/common.h"
namespace input {

struct input_t;

void print(void *input);
void reset(std::shared_ptr<input_t> &input);
void passthrough(std::shared_ptr<input_t> &input, std::vector<std::uint8_t> &&input_data);


void init();

std::shared_ptr<input_t> alloc();

using touch_port_event_t = std::unique_ptr<safe::event_t<platf::touch_port_t>>;
extern touch_port_event_t touch_port_event;
}

#endif //SUNSHINE_INPUT_H
