//
// Created by Shawn Xiong on 5/7/2023 to support dual displays
// concept:
// The display_dual_t creates two instances of display_vram_t and connect each with real display
//

#include "display_dual.h"

namespace platf::dxgi {

  //Shawn Xiong
  //dual_img_t wraps two img_t, and allocated by one of device from dual display
  //so keep display pointer in this struct also
  struct dual_img_t: public platf::img_t {
    std::shared_ptr<img_t> img1, img2;
    std::shared_ptr<platf::display_t> display;

    // These objects are owned by the display_t's ID3D11Device
    texture2d_t capture_texture;
    render_target_t capture_rt;
    keyed_mutex_t capture_mutex;

    // This is the shared handle used by hwdevice_t to open capture_texture
    HANDLE encoder_texture_handle = {};

    // Set to true if the image corresponds to a dummy texture used prior to
    // the first successful capture of a desktop frame
    bool dummy = false;

    // Unique identifier for this image
    uint32_t id = 0;

    virtual ~dual_img_t() override {
      if (encoder_texture_handle) {
        CloseHandle(encoder_texture_handle);
      }
    };
  };

  capture_e
  display_dual_t::snapshot(img_t *img, std::chrono::milliseconds timeout, bool cursor_visible) {
    return capture_e();
  }
  std::shared_ptr<img_t>
  display_dual_t::alloc_img() {
    auto img = std::make_shared<dual_img_t>();
    img->width = width;
    img->height = height;
    img->display = shared_from_this();
    img->id = next_image_id++;

    return img;
  }
  int
  display_dual_t::dummy_img(img_t *img_base) {
    return complete_img(img_base, true);
  }
  int
  display_dual_t::complete_img(img_t *img_base, bool dummy) {
    if (m_disp1) {
      return m_disp1->complete_img(img_base, dummy);
    }
    else {
      return -1;
    }
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
    if (m_disp2) {
      m_disp2->make_hwdevice(pix_fmt);
    }
    if (m_disp1) {
      return m_disp1->make_hwdevice(pix_fmt);
    }
    else {
      return std::shared_ptr<platf::hwdevice_t>();
    }
  }
  //help function to create display_t
  std::shared_ptr<display_base_t>
  display_dual_t::MakeDisp(mem_type_e hwdevice_type, const ::video::config_t &config,
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
  display_dual_t::init(mem_type_e hwdevice_type, const ::video::config_t &config,
    const std::string &display_name) {
    std::size_t pos = display_name.find(display_name_separator);
    if (pos == std::string::npos) {
      return -1;
    }
    //TODO(Shawn): we need to choose a better device(GPU) as m_disp1,
    //and we use m_disp1 to allocate a big texture to hold dual display's capture textures
    //so for merge operation just means copy second display's texture into the big texture's right side
    //use CopyResource API can copy texture from one device to another device( if different device)
    auto disp1_name = display_name.substr(0, pos);
    auto disp2_name = display_name.substr(pos + 1);
    m_disp1 = MakeDisp(hwdevice_type, config, disp1_name);
    m_disp2 = MakeDisp(hwdevice_type, config, disp2_name);
    if (m_disp1 == nullptr || m_disp2 == nullptr) {
      return -1;
    }
    width = m_disp1->width + m_disp2->width;
    m_disp1->output_x_offset = 0;
    m_disp2->output_x_offset = m_disp1->width;
    height = (m_disp1->height > m_disp2->height) ? m_disp1->height : m_disp2->height;
    //todo:debug
    width = width / 2;
    return 0;
  }
}  // namespace platf::dxgi