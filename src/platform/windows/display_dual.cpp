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
  //help function to create display_t
  std::shared_ptr<display_base_t>
  display_dual_t::MakeDisp(mem_type_e hwdevice_type,const ::video::config_t &config,
      const std::string &display_name) {
    if (hwdevice_type == mem_type_e::dxgi) {
      auto disp = std::make_shared<dxgi::display_vram_t>();

      if (!disp->init(config, display_name)) {
        return disp;
      }
    }
    else if (hwdevice_type == mem_type_e::system) {
      auto disp = std::make_shared<dxgi::display_ram_t>();

      if (!disp->init(config, display_name)) {
        return disp;
      }
    }
    return std::shared_ptr<display_base_t>();
  }

  int
  display_dual_t::init(mem_type_e hwdevice_type,const ::video::config_t &config, 
      const std::string &display_name) {
    std::size_t pos = display_name.find(display_name_separator);
    if (pos == std::string::npos) {
      return 0;
    }
    auto disp1_name = display_name.substr(0, pos);
    auto disp2_name = display_name.substr(pos+1);
    m_disp1 = MakeDisp(hwdevice_type,config, disp1_name);
    m_disp2 = MakeDisp(hwdevice_type,config, disp2_name);
    //TODO: base on m_disp1, and m_disp2, to calculate the layout of the merged frame

    return 0;
  }
}  // namespace platf::dxgi