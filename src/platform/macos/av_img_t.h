/**
 * @file src/platform/macos/av_img_t.h
 * @brief todo
 */
#pragma once

#include "src/platform/common.h"

#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>

namespace platf {
  struct av_sample_buf_t {
    av_sample_buf_t(CMSampleBufferRef buf):
        buf((CMSampleBufferRef) CFRetain(buf)) {}

    ~av_sample_buf_t() {
      CFRelease(buf);
    }

    CMSampleBufferRef buf;
  };

  struct av_pixel_buf_t {
    av_pixel_buf_t(CVPixelBufferRef buf):
        buf((CVPixelBufferRef) CFRetain(buf)),
        locked(false) {}

    uint8_t *
    lock() {
      if (!locked) {
        CVPixelBufferLockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
      }
      return (uint8_t *) CVPixelBufferGetBaseAddress(buf);
    }

    ~av_pixel_buf_t() {
      if (locked) {
        CVPixelBufferUnlockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
      }
      CFRelease(buf);
    }

    CVPixelBufferRef buf;
    bool locked;
  };

  struct av_img_t: public img_t {
    std::shared_ptr<av_sample_buf_t> sample_buffer;
    std::shared_ptr<av_pixel_buf_t> pixel_buffer;
  };
}  // namespace platf
