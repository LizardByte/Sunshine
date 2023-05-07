//
// Created by Shawn Xiong on 5/7/2023 to support dual displays
// concept: 
// The display_dual_t creates two instances of display_vram_t and connect each with real display 
//

#include "display_dual.h"


namespace platf::dxgi {
  capture_e
  display_dual_t::snapshot(img_t *img, std::chrono::milliseconds timeout, bool cursor_visible) {

    return capture_e();
  }
}  // namespace platf::dxgi