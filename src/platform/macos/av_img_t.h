/**
 * @file src/platform/macos/av_img_t.h
 * @brief Declarations for AV image types on macOS.
 */
#pragma once

// platform includes
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>

// local includes
#include "src/platform/common.h"

namespace platf {
  /**
   * @brief CoreMedia sample buffer retained by an AV image wrapper.
   */
  struct av_sample_buf_t {
    CMSampleBufferRef buf;  ///< Retained CoreMedia sample buffer.

    /**
     * @brief Retain a CoreMedia sample buffer for captured-image lifetime.
     *
     * @param buf Sample buffer received from AVFoundation.
     */
    explicit av_sample_buf_t(CMSampleBufferRef buf):
        buf((CMSampleBufferRef) CFRetain(buf)) {
    }

    ~av_sample_buf_t() {
      if (buf != nullptr) {
        CFRelease(buf);
      }
    }
  };

  /**
   * @brief CoreVideo pixel buffer retained by an AV image wrapper.
   */
  struct av_pixel_buf_t {
    CVPixelBufferRef buf;  ///< Pixel buffer extracted from the sample buffer.

    // Constructor
    /**
     * @brief Lock the sample buffer's pixel buffer for read-only access.
     *
     * @param sb Sample buffer that owns the image data.
     */
    explicit av_pixel_buf_t(CMSampleBufferRef sb):
        buf(
          CMSampleBufferGetImageBuffer(sb)
        ) {
      CVPixelBufferLockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
    }

    /**
     * @brief Return the base address of the locked Core Video pixel buffer.
     *
     * @return Pointer to the first byte of image data in the pixel buffer.
     */
    [[nodiscard]] uint8_t *data() const {
      return static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(buf));
    }

    // Destructor
    ~av_pixel_buf_t() {
      if (buf != nullptr) {
        CVPixelBufferUnlockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
      }
    }
  };

  /**
   * @brief Captured macOS image backed by an AV sample buffer.
   */
  struct av_img_t: img_t {
    std::shared_ptr<av_sample_buf_t> sample_buffer;  ///< Retained AVFoundation sample buffer for the captured frame.
    std::shared_ptr<av_pixel_buf_t> pixel_buffer;  ///< Retained CoreVideo pixel buffer extracted from the sample.
  };

  /**
   * @brief Temporary retain wrapper used while AV image data is borrowed.
   */
  struct temp_retain_av_img_t {
    std::shared_ptr<av_sample_buf_t> sample_buffer;  ///< Sample buffer kept alive while `data` is borrowed.
    std::shared_ptr<av_pixel_buf_t> pixel_buffer;  ///< Pixel buffer kept locked while `data` is borrowed.
    uint8_t *data;  ///< Pointer to the locked pixel buffer bytes.

    /**
     * @brief Construct a temporary AV image retain wrapper.
     *
     * @param sb Sample buffer to retain.
     * @param pb Pixel buffer to retain.
     * @param dt Image data pointer.
     */
    temp_retain_av_img_t(
      std::shared_ptr<av_sample_buf_t> sb,
      std::shared_ptr<av_pixel_buf_t> pb,
      uint8_t *dt
    ):
        sample_buffer(std::move(sb)),
        pixel_buffer(std::move(pb)),
        data(dt) {
    }
  };
}  // namespace platf
