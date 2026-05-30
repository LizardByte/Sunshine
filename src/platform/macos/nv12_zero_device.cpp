/**
 * @file src/platform/macos/nv12_zero_device.cpp
 * @brief Definitions for NV12 zero copy device on macOS.
 */
// standard includes
#include <CoreFoundation/CoreFoundation.h>
#include <cstddef>
#include <cstdint>
#include <utility>

// platform includes
#include <CoreVideo/CVPixelBufferIOSurface.h>

// local includes
#include "src/logging.h"
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

  nv12_zero_device::~nv12_zero_device() {
    if (transfer_session) {
      VTPixelTransferSessionInvalidate(transfer_session);
      CFRelease(transfer_session);
    }

    if (pixel_buffer_pool) {
      CFRelease(pixel_buffer_pool);
    }
  }

  int nv12_zero_device::attach_pixel_buffer(CVPixelBufferRef pixel_buffer) {
    av_buffer_unref(&av_frame->buf[0]);
    av_frame->buf[0] = av_buffer_create((uint8_t *) pixel_buffer, 0, free_buffer, nullptr, 0);
    if (!av_frame->buf[0]) {
      CVPixelBufferRelease(pixel_buffer);
      return -1;
    }

    // Place a CVPixelBufferRef at data[3] as required by AV_PIX_FMT_VIDEOTOOLBOX
    av_frame->data[3] = (uint8_t *) pixel_buffer;

    return 0;
  }

  int nv12_zero_device::ensure_pixel_buffer_pool(OSType pixel_format) {
    if (pixel_buffer_pool && pool_pixel_format == pixel_format && pool_width == av_frame->width && pool_height == av_frame->height) {
      return 0;
    }

    if (pixel_buffer_pool) {
      CFRelease(pixel_buffer_pool);
      pixel_buffer_pool = nullptr;
    }
    black_surfaces.clear();

    auto width = av_frame->width;
    auto height = av_frame->height;
    auto pixel_format_value = static_cast<std::int32_t>(pixel_format);
    CFNumberRef width_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &width);
    CFNumberRef height_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &height);
    CFNumberRef pixel_format_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixel_format_value);
    CFDictionaryRef io_surface_properties = CFDictionaryCreate(
      kCFAllocatorDefault,
      nullptr,
      nullptr,
      0,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks
    );

    const void *keys[] = {
      kCVPixelBufferPixelFormatTypeKey,
      kCVPixelBufferWidthKey,
      kCVPixelBufferHeightKey,
      kCVPixelBufferIOSurfacePropertiesKey,
    };
    const void *values[] = {
      pixel_format_number,
      width_number,
      height_number,
      io_surface_properties,
    };
    CFDictionaryRef attrs = CFDictionaryCreate(
      kCFAllocatorDefault,
      keys,
      values,
      4,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks
    );

    auto status = CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr, attrs, &pixel_buffer_pool);

    CFRelease(attrs);
    CFRelease(io_surface_properties);
    CFRelease(pixel_format_number);
    CFRelease(height_number);
    CFRelease(width_number);

    if (status != kCVReturnSuccess || !pixel_buffer_pool) {
      BOOST_LOG(error) << "Failed to create VideoToolbox pixel transfer pool: " << status;
      return -1;
    }

    pool_pixel_format = pixel_format;
    pool_width = width;
    pool_height = height;

    return 0;
  }

  int nv12_zero_device::ensure_transfer_session() {
    if (transfer_session) {
      return 0;
    }

    auto status = VTPixelTransferSessionCreate(kCFAllocatorDefault, &transfer_session);
    if (status != noErr || !transfer_session) {
      BOOST_LOG(error) << "Failed to create VideoToolbox pixel transfer session: " << status;
      return -1;
    }

    VTSessionSetProperty(transfer_session, kVTPixelTransferPropertyKey_ScalingMode, kVTScalingMode_CropSourceToCleanAperture);
    VTSessionSetProperty(transfer_session, kVTPixelTransferPropertyKey_RealTime, kCFBooleanTrue);
    VTSessionSetProperty(transfer_session, kVTPixelTransferPropertyKey_DownsamplingMode, kVTDownsamplingMode_Average);

    return 0;
  }

  int nv12_zero_device::prefill_black(CVPixelBufferRef pixel_buffer) {
    auto surface = CVPixelBufferGetIOSurface(pixel_buffer);
    if (surface && black_surfaces.find(surface) != black_surfaces.end()) {
      return 0;
    }

    auto pixel_format = CVPixelBufferGetPixelFormatType(pixel_buffer);
    AVPixelFormat av_pixel_format;
    switch (pixel_format) {
      case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
        av_pixel_format = AV_PIX_FMT_NV12;
        break;
      case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
        av_pixel_format = AV_PIX_FMT_P010;
        break;
      default:
        av_pixel_format = AV_PIX_FMT_NONE;
        break;
    }

    if (av_pixel_format == AV_PIX_FMT_NONE) {
      BOOST_LOG(error) << "Unsupported pixel format for VideoToolbox black fill: " << pixel_format;
      return -1;
    }

    auto status = CVPixelBufferLockBaseAddress(pixel_buffer, 0);
    if (status != kCVReturnSuccess) {
      BOOST_LOG(error) << "Failed to lock VideoToolbox pixel transfer buffer: " << status;
      return -1;
    }

    uint8_t *data[4] {};
    ptrdiff_t linesize[4] {};
    auto plane_count = CVPixelBufferGetPlaneCount(pixel_buffer);
    for (size_t plane = 0; plane < plane_count && plane < 4; ++plane) {
      data[plane] = static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, plane));
      linesize[plane] = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, plane);
    }

    if (plane_count < 2 || !data[0] || !data[1]) {
      CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);
      BOOST_LOG(error) << "VideoToolbox pixel transfer buffer has no writable YUV planes";
      return -1;
    }

    auto result = av_image_fill_black(
      data,
      linesize,
      av_pixel_format,
      AVCOL_RANGE_MPEG,
      static_cast<int>(CVPixelBufferGetWidth(pixel_buffer)),
      static_cast<int>(CVPixelBufferGetHeight(pixel_buffer))
    );
    CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);

    if (result < 0) {
      BOOST_LOG(error) << "Failed to black-fill VideoToolbox pixel transfer buffer: " << result;
      return -1;
    }

    if (surface) {
      black_surfaces.insert(surface);
    }

    return 0;
  }

  CVPixelBufferRef nv12_zero_device::copy_scaled_pixel_buffer(CVPixelBufferRef pixel_buffer) {
    auto source_pixel_format = CVPixelBufferGetPixelFormatType(pixel_buffer);
    if (source_pixel_format == output_pixel_format && CVPixelBufferGetWidth(pixel_buffer) == av_frame->width && CVPixelBufferGetHeight(pixel_buffer) == av_frame->height) {
      return (CVPixelBufferRef) CFRetain(pixel_buffer);
    }

    if (ensure_pixel_buffer_pool(output_pixel_format) || ensure_transfer_session()) {
      return nullptr;
    }

    CVPixelBufferRef scaled_pixel_buffer = nullptr;
    auto status = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pixel_buffer_pool, &scaled_pixel_buffer);
    if (status != kCVReturnSuccess || !scaled_pixel_buffer) {
      BOOST_LOG(error) << "Failed to create VideoToolbox pixel transfer buffer: " << status;
      return nullptr;
    }

    if (prefill_black(scaled_pixel_buffer)) {
      CVPixelBufferRelease(scaled_pixel_buffer);
      return nullptr;
    }

    auto width_scale = (double) av_frame->width / (double) CVPixelBufferGetWidth(pixel_buffer);
    auto height_scale = (double) av_frame->height / (double) CVPixelBufferGetHeight(pixel_buffer);
    auto scale = width_scale < height_scale ? width_scale : height_scale;
    auto clean_width = (double) CVPixelBufferGetWidth(pixel_buffer) * scale;
    auto clean_height = (double) CVPixelBufferGetHeight(pixel_buffer) * scale;
    double zero = 0.0;
    CFNumberRef clean_width_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &clean_width);
    CFNumberRef clean_height_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &clean_height);
    CFNumberRef clean_horizontal_offset = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &zero);
    CFNumberRef clean_vertical_offset = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &zero);
    const void *clean_keys[] = {
      kCVImageBufferCleanApertureWidthKey,
      kCVImageBufferCleanApertureHeightKey,
      kCVImageBufferCleanApertureHorizontalOffsetKey,
      kCVImageBufferCleanApertureVerticalOffsetKey,
    };
    const void *clean_values[] = {
      clean_width_number,
      clean_height_number,
      clean_horizontal_offset,
      clean_vertical_offset,
    };
    CFDictionaryRef clean_aperture = CFDictionaryCreate(
      kCFAllocatorDefault,
      clean_keys,
      clean_values,
      4,
      &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks
    );
    VTSessionSetProperty(transfer_session, kVTPixelTransferPropertyKey_DestinationCleanAperture, clean_aperture);
    CFRelease(clean_aperture);
    CFRelease(clean_vertical_offset);
    CFRelease(clean_horizontal_offset);
    CFRelease(clean_height_number);
    CFRelease(clean_width_number);

    status = VTPixelTransferSessionTransferImage(transfer_session, pixel_buffer, scaled_pixel_buffer);
    if (status != noErr) {
      BOOST_LOG(error) << "VideoToolbox pixel transfer failed: " << status;
      CVPixelBufferRelease(scaled_pixel_buffer);
      return nullptr;
    }

    return scaled_pixel_buffer;
  }

  int nv12_zero_device::convert(platf::img_t &img) {
    auto *av_img = (av_img_t *) &img;

    if (!av_img->pixel_buffer || !av_img->pixel_buffer->buf) {
      return -1;
    }

    CVPixelBufferRef pixel_buffer = copy_scaled_pixel_buffer(av_img->pixel_buffer->buf);
    if (!pixel_buffer) {
      return -1;
    }

    return attach_pixel_buffer(pixel_buffer);
  }

  int nv12_zero_device::set_frame(AVFrame *frame, AVBufferRef *hw_frames_ctx) {
    this->frame = frame;

    av_frame.reset(frame);

    if (resize_capture) {
      resolution_fn(this->display, frame->width, frame->height);
    }

    return 0;
  }

  int nv12_zero_device::init(void *display, pix_fmt_e pix_fmt, resolution_fn_t resolution_fn, const pixel_format_fn_t &pixel_format_fn, bool resize_capture) {
    output_pixel_format = pix_fmt == pix_fmt_e::nv12 ? kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
    pixel_format_fn(display, output_pixel_format);

    this->display = display;
    this->resolution_fn = std::move(resolution_fn);
    this->resize_capture = resize_capture;

    // we never use this pointer, but its existence is checked/used
    // by the platform independent code
    data = this;

    return 0;
  }

}  // namespace platf
