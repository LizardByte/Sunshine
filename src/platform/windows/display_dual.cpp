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
  std::shared_ptr<img_t>
  display_dual_t::alloc_img() {
    return std::shared_ptr<img_t>();
  }
  int
  display_dual_t::dummy_img(img_t *img_base) {
    return 0;
  }
  int
  display_dual_t::complete_img(img_t *img_base, bool dummy) {
    return 0;
  }
  std::vector<DXGI_FORMAT>
  display_dual_t::get_supported_sdr_capture_formats() {
    return std::vector<DXGI_FORMAT>();
  }
  std::vector<DXGI_FORMAT>
  display_dual_t::get_supported_hdr_capture_formats() {
    return std::vector<DXGI_FORMAT>();
  }
  std::shared_ptr<platf::hwdevice_t>
  display_dual_t::make_hwdevice(pix_fmt_e pix_fmt) {
    return std::shared_ptr<platf::hwdevice_t>();
  }
  int
  display_dual_t::init(const ::video::config_t &config, const std::string &display_name) {
    return 0;
  }
}  // namespace platf::dxgi