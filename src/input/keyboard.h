#pragma once

#include <cstdint>
extern "C" {
#include <moonlight-common-c/src/Input.h>
#include <moonlight-common-c/src/Limelight.h>
}

#include <functional>

#include "common.h"
#include "src/platform/common.h"
#include "src/thread_safe.h"

namespace input::keyboard {
  typedef uint32_t key_press_id_t;

  void
  print(PNV_KEYBOARD_PACKET packet);

  void
  print(PNV_UNICODE_PACKET packet);

  void
  passthrough(std::shared_ptr<input_t> &input, PNV_KEYBOARD_PACKET packet);

  void
  passthrough(PNV_UNICODE_PACKET packet);

  void
  reset(platf::input_t &platf_input);

  void
  cancel();
}  // namespace input::keyboard
