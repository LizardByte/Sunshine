#include "platform_input.h"

platf::input_t PlatformInput::_instance;

platf::input_t &
PlatformInput::getInstance() {
  return _instance;
}
