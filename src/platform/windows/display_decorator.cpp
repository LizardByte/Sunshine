
#include "display_decorator.h"

#include "../common.h"  // For capture_e
#include "display.h"
#include "src/globals.h"
#include "src/logging.h"  // For BOOST_LOG(info)

namespace platf::dxgi {

  display_vram_decorator_t::display_vram_decorator_t():
      start_time_(std::chrono::steady_clock::now()) {
    BOOST_LOG(info) << "Initializing display_vram_decorator_t (DXGI VRAM capture)";
  }

  platf::capture_e display_vram_decorator_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    if (elapsed.count() >= 10) {
      start_time_ = now;
      mail::man->event<bool>(mail::wgc_switch)->raise(true);
      return platf::capture_e::swap_capture;
    }
    return display_vram_t::snapshot(pull_free_image_cb, img_out, timeout, cursor_visible);
  }

  display_ddup_vram_decorator_t::display_ddup_vram_decorator_t():
      start_time_(std::chrono::steady_clock::now()) {
    BOOST_LOG(info) << "Initializing display_ddup_vram_decorator_t (DXGI Desktop Duplication VRAM capture)";
  }

  platf::capture_e display_ddup_vram_decorator_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    if (elapsed.count() >= 10) {
      start_time_ = now;
      mail::man->event<bool>(mail::wgc_switch)->raise(true);
      return platf::capture_e::swap_capture;
    }
    return display_ddup_vram_t::snapshot(pull_free_image_cb, img_out, timeout, cursor_visible);
  }

  display_wgc_vram_decorator_t::display_wgc_vram_decorator_t():
      start_time_(std::chrono::steady_clock::now()) {
    BOOST_LOG(info) << "Initializing display_wgc_vram_decorator_t (WGC VRAM capture)";
  }

  platf::capture_e display_wgc_vram_decorator_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    if (elapsed.count() >= 10) {
      start_time_ = now;
      return platf::capture_e::swap_capture;
    }
    return display_wgc_vram_t::snapshot(pull_free_image_cb, img_out, timeout, cursor_visible);
  }

}  // namespace platf::dxgi
