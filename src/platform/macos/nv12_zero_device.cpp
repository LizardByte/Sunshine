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

  void free_frame(AVFrame *frame) {
    av_frame_free(&frame);
  }

  void free_buffer(void *opaque, uint8_t *data) {
    CVPixelBufferRelease((CVPixelBufferRef) data);
  }

  util::safe_ptr<AVFrame, free_frame> av_frame;

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

    resolution_fn(this->display, frame->width, frame->height);

    return 0;
  }

  int nv12_zero_device::init(void *display, pix_fmt_e pix_fmt, resolution_fn_t resolution_fn, const pixel_format_fn_t &pixel_format_fn) {
    // Map the abstract pix_fmt_e to the matching CVPixelBufferType. The
    // 4:2:0 BiPlanar formats (NV12 / P010) cover H.264 / HEVC / AV1; the
    // 4:4:4 BiPlanar formats (NV24 / P410) cover ProRes (422 profiles via
    // encoder-internal downsample, 4444 profiles natively).
    OSType cv_format;
    switch (pix_fmt) {
      case pix_fmt_e::nv12:
        cv_format = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        break;
      case pix_fmt_e::nv24:
        cv_format = kCVPixelFormatType_444YpCbCr8BiPlanarVideoRange;
        break;
      case pix_fmt_e::p410:
        cv_format = kCVPixelFormatType_444YpCbCr10BiPlanarVideoRange;
        break;
      case pix_fmt_e::p010:
      default:
        // p010 is the historical 10-bit 4:2:0 path; the default fall-through
        // matches it because display.mm::make_avcodec_encode_device is the
        // source of truth for which pix_fmt values reach this method.
        cv_format = kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
        break;
    }
    pixel_format_fn(display, cv_format);

    this->display = display;
    this->resolution_fn = std::move(resolution_fn);

    // we never use this pointer, but its existence is checked/used
    // by the platform independent code
    data = this;

    return 0;
  }

}  // namespace platf
