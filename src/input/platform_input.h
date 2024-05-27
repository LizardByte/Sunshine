#pragma once

#include "src/platform/common.h"

class PlatformInput {
public:
  static platf::input_t _instance;

  static platf::input_t &
  getInstance();

private:
  PlatformInput() = default;
};
