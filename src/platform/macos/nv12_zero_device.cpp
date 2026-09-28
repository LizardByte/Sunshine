/**
 * @file src/platform/macos/nv12_zero_device.cpp
 * @brief Definitions for NV12 zero copy device on macOS.
 */
// standard includes
#include <utility>

// local includes
#include "src/platform/macos/av_img_t.h"
#include "src/platform/macos/nv12_zero_device.h"
#include "src/video.h"

extern "C" {
#include "libavutil/imgutils.h"
}

namespace platf {

  /**
   * @brief Release an FFmpeg frame allocated by the capture or conversion backend.
   */
  void free_frame(AVFrame *frame) {
    av_frame_free(&frame);
  }

  /**
   * @brief Release a backend buffer allocated for capture or conversion.
   *
   * @param opaque Opaque user pointer provided to the callback.
   * @param data Payload or state data to serialize, deserialize, or forward.
   */
  void free_buffer(void *opaque, uint8_t *data) {
    CVPixelBufferRelease((CVPixelBufferRef) data);
  }

  int nv12_zero_device::convert(platf::img_t &img) {
    auto *av_img = (av_img_t *) &img;

    // Release any existing CVPixelBuffer previously retained for encoding
    av_buffer_unref(&av_frame->buf[0]);

    // Attach an AVBufferRef to this frame which will retain ownership of the CVPixelBuffer
    // until av_buffer_unref() is called (above) or the frame is freed with av_frame_free().
    //
    // The presence of the AVBufferRef allows FFmpeg to simply add a reference to the buffer
    // rather than having to perform a deep copy of the data buffers in avcodec_send_frame().
    av_frame->buf[0] = av_buffer_create((uint8_t *) CFRetain(av_img->pixel_buffer->buf), 0, free_buffer, nullptr, 0);

    // Place a CVPixelBufferRef at data[3] as required by AV_PIX_FMT_VIDEOTOOLBOX
    av_frame->data[3] = (uint8_t *) av_img->pixel_buffer->buf;

    return 0;
  }

  int nv12_zero_device::set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) {
    this->frame = frame;

    av_frame.reset(frame);

    resolution_fn(this->sc, frame->width, frame->height);

    return 0;
  }

  int nv12_zero_device::init(::screen_capture *sc, pix_fmt_e pix_fmt, bool full_range, resolution_fn_t resolution_fn, const pixel_format_fn_t &pixel_format_fn) {
    OSType pixelFormat;
    switch (pix_fmt) {
      case pix_fmt_e::p010:
        pixelFormat = full_range ? kCVPixelFormatType_420YpCbCr10BiPlanarFullRange : kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
        break;
      case pix_fmt_e::nv24:
        pixelFormat = full_range ? kCVPixelFormatType_444YpCbCr8BiPlanarFullRange : kCVPixelFormatType_444YpCbCr8BiPlanarVideoRange;
        break;
      case pix_fmt_e::p410:
        pixelFormat = full_range ? kCVPixelFormatType_444YpCbCr10BiPlanarFullRange : kCVPixelFormatType_444YpCbCr10BiPlanarVideoRange;
        break;
      case pix_fmt_e::nv12:
      default:
        pixelFormat = full_range ? kCVPixelFormatType_420YpCbCr8BiPlanarFullRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        break;
    }
    pixel_format_fn(sc, pixelFormat);

    this->sc = sc;
    this->resolution_fn = std::move(resolution_fn);

    // we never use this pointer, but its existence is checked/used
    // by the platform independent code
    data = this;

    return 0;
  }

}  // namespace platf
