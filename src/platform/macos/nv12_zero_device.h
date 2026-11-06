/**
 * @file src/platform/macos/nv12_zero_device.h
 * @brief Declarations for NV12 zero copy device on macOS.
 */
#pragma once

// local includes
#include "src/platform/common.h"

struct AVFrame;
struct screen_capture;

#ifdef __OBJC__
@class AVVideo;
#else
/**
 * @brief Opaque AVFoundation capture controller, defined in Objective-C.
 */
struct AVVideo;
#endif

namespace platf {
  void free_frame(AVFrame *frame);

  class nv12_zero_device: public avcodec_encode_device_t {
    ::screen_capture *sc;

  public:
    /**
     * @brief Callback signature used to update the opaque AVFoundation display resolution.
     */
    using resolution_fn_t = std::function<void(::screen_capture *sc, int width, int height)>;
    resolution_fn_t resolution_fn;

    /**
     * @brief Callback signature used to update the opaque AVFoundation pixel format.
     */
    using pixel_format_fn_t = std::function<void(::screen_capture *sc, int pixelFormat)>;

    /**
     * @brief Initialize zero-copy NV12 encoding for an AVFoundation display.
     *
     * @param sc Display object or identifier associated with the operation.
     * @param pix_fmt Sunshine pixel format to convert or allocate for.
     * @param full_range True for full range color.
     * @param set_resolution Callback used to resize the AVFoundation capture output.
     * @param pixel_format_fn Pixel format.
     * @return 0 on success; nonzero or negative platform status on failure.
     */
    int init(::screen_capture *sc, pix_fmt_e pix_fmt, bool full_range, resolution_fn_t resolution_fn, const pixel_format_fn_t &pixel_format_fn);

    int convert(img_t &img) override;

    /**
     * @brief Attach frame resources used by the next conversion or encode operation.
     *
     * @param frame Video or graphics frame being processed.
     * @param hw_frames_ctx FFmpeg hardware frames context associated with the frame.
     * @return Status from updating frame.
     */
    int set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) override;

  private:
    util::safe_ptr<AVFrame, &free_frame> av_frame;
  };

}  // namespace platf
