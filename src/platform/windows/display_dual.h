//
// Created by Shawn Xiong on 5/7/2023 to support dual displays
//

#ifndef SUNSHINE_DUAL_DISPLAY_H
#define SUNSHINE_DUAL_DISPLAY_H

#include "display.h"

namespace platf::dxgi {
  class display_dual_t: public display_base_t, public std::enable_shared_from_this<display_dual_t> {
  protected:
    std::shared_ptr<platf::display_vram_t> disp1, disp2;

  public:
    virtual capture_e
    snapshot(img_t *img, std::chrono::milliseconds timeout, bool cursor_visible) override;
  };
}  // namespace platf::dxgi
#endif  //SUNSHINE_DUAL_DISPLAY_H