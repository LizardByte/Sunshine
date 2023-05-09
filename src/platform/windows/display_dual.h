//
// Created by Shawn Xiong on 5/7/2023 to support dual displays
//

#ifndef SUNSHINE_DUAL_DISPLAY_H
#define SUNSHINE_DUAL_DISPLAY_H

#include "display.h"

namespace platf::dxgi {
  class display_dual_t: public display_base_t, public std::enable_shared_from_this<display_dual_t> {
  protected:
    std::atomic<uint32_t> next_image_id;
    std::shared_ptr<display_base_t> m_disp1, m_disp2;
    std::shared_ptr<display_base_t>
    MakeDisp(mem_type_e hwdevice_type,const ::video::config_t &config, const std::string &display_name);
  public:
    virtual capture_e
    snapshot(img_t *img, std::chrono::milliseconds timeout, bool cursor_visible) override;
    std::shared_ptr<img_t>
    alloc_img() override;
    int
    dummy_img(img_t *img_base) override;
    int
    complete_img(img_t *img_base, bool dummy) override;
    std::vector<DXGI_FORMAT>
    get_supported_sdr_capture_formats() override;
    std::vector<DXGI_FORMAT>
    get_supported_hdr_capture_formats() override;
    std::shared_ptr<platf::hwdevice_t>
    make_hwdevice(pix_fmt_e pix_fmt) override;
    virtual bool
    is_group() override {
      return false;
    }
    virtual std::shared_ptr<display_t>
    get_item(int index) override {
      switch (index) {
        case 0:
          return m_disp1;
        case 1:
          return m_disp2;
        default:
          break;
      }
      return std::shared_ptr<display_t>();
    }
    int
    init(mem_type_e hwdevice_type,const ::video::config_t &config, const std::string &display_name);
  };
}  // namespace platf::dxgi
#endif  //SUNSHINE_DUAL_DISPLAY_H