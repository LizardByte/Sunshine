#pragma once
#include "display.h"
#include <chrono>
#include <memory>


namespace platf::dxgi {

// Decorator for display_vram_t (DXGI VRAM Capture)
class display_vram_decorator_t : public display_vram_t {
public:
    display_vram_decorator_t();
    platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) override;
private:
    std::chrono::steady_clock::time_point start_time_;
};

// Decorator for display_ddup_vram_t (DXGI Desktop Duplication VRAM Capture)
class display_ddup_vram_decorator_t : public display_ddup_vram_t {
public:
    display_ddup_vram_decorator_t();
    platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) override;
private:
    std::chrono::steady_clock::time_point start_time_;
};

// Decorator for display_wgc_vram_t (WGC VRAM Capture)
class display_wgc_vram_decorator_t : public display_wgc_vram_t {
public:
    display_wgc_vram_decorator_t();
    platf::capture_e snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) override;
private:
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace platf::dxgi
