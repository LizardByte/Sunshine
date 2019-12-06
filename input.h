//
// Created by loki on 6/20/19.
//

#ifndef SUNSHINE_INPUT_H
#define SUNSHINE_INPUT_H

#include <platform/common.h>

namespace input {
void print(void *input);

void passthrough(platf::input_t &, void *input);
}

#endif //SUNSHINE_INPUT_H
