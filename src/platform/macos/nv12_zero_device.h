/**
 * @file src/platform/macos/nv12_zero_device.h
 * @brief Declarations for NV12 zero copy device on macOS.
 */
#pragma once

// local includes
#include "src/platform/common.h"

struct AVFrame;
struct screen_capture;

namespace platf {
  void free_frame(AVFrame *frame);

  class nv12_zero_device: public avcodec_encode_device_t {
    ::screen_capture *sc;

  public:
    using resolution_fn_t = std::function<void(::screen_capture *sc, int width, int height)>;
    resolution_fn_t resolution_fn;
    using pixel_format_fn_t = std::function<void(::screen_capture *sc, int pixelFormat)>;

    int init(::screen_capture *sc, pix_fmt_e pix_fmt, bool full_range, resolution_fn_t resolution_fn, const pixel_format_fn_t &pixel_format_fn);

    int convert(img_t &img) override;
    int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) override;

  private:
    util::safe_ptr<AVFrame, free_frame> av_frame;
  };

}  // namespace platf
