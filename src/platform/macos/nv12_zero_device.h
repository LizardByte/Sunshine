/**
 * @file src/platform/macos/nv12_zero_device.h
 * @brief Declarations for NV12 zero copy device on macOS.
 */
#pragma once

// local includes
#include "src/platform/common.h"

struct AVFrame;

namespace platf {
  /**
   * @brief Release an FFmpeg frame allocated by the capture or conversion backend.
   *
   * @param frame Video or graphics frame being processed.
   */
  void free_frame(AVFrame *frame);

  /**
   * @brief macOS zero-copy encode device that forwards NV12 frames from AVFoundation to FFmpeg.
   */
  class nv12_zero_device: public avcodec_encode_device_t {
    // display holds a pointer to an av_video object. Since the namespaces of AVFoundation
    // and FFMPEG collide, we need this opaque pointer and cannot use the definition
    void *display;

  public:
    // this function is used to set the resolution on an av_video object that we cannot
    // call directly because of namespace collisions between AVFoundation and FFMPEG
    /**
     * @brief Callback signature used to update the opaque AVFoundation display resolution.
     */
    using resolution_fn_t = std::function<void(void *display, int width, int height)>;
    resolution_fn_t resolution_fn;  ///< Callback stored for later AVFoundation resolution updates.
    /**
     * @brief Callback signature used to update the opaque AVFoundation pixel format.
     */
    using pixel_format_fn_t = std::function<void(void *display, int pixelFormat)>;

    /**
     * @brief Initialize zero-copy NV12 encoding for an AVFoundation display.
     *
     * @param display Display object or identifier associated with the operation.
     * @param pix_fmt Sunshine pixel format to convert or allocate for.
     * @param resolution_fn Callback used to resize the AVFoundation capture output.
     * @param pixel_format_fn Pixel format.
     * @return 0 on success; nonzero or negative platform status on failure.
     */
    int init(void *display, pix_fmt_e pix_fmt, resolution_fn_t resolution_fn, const pixel_format_fn_t &pixel_format_fn);

    /**
     * @brief Forward a captured AVFoundation frame to FFmpeg without copying.
     *
     * @param img Image or frame object to read from or populate.
     * @return Conversion status.
     */
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
    util::safe_ptr<AVFrame, free_frame> av_frame;
  };

}  // namespace platf
