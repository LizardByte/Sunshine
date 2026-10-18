/**
 * @file src/platform/macos/sc_video.h
 * @brief Declarations for ScreenCaptureKit video capture on macOS.
 */
#pragma once

// local includes
#import "av_video.h"

// platform includes
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#ifdef __cplusplus
extern "C" {
#endif

  /**
   * @brief Copy a captured frame into a buffer from `pool`, keeping its color attachments and timing.
   *
   * @param source Sample buffer holding the captured frame.
   * @param pool Pool for the copies; (re)created when the frame format or size changes. Release with CVPixelBufferPoolRelease().
   * @return Retained copy, or NULL on failure.
   */
  CMSampleBufferRef sc_copy_sample_buffer(CMSampleBufferRef source, CVPixelBufferPoolRef *pool) CF_RETURNS_RETAINED;

#ifdef __cplusplus
}
#endif

/**
 * @brief ScreenCaptureKit capture backend with HDR (BT.2020 PQ) support.
 *
 * Drop-in replacement for AVVideo: same properties, same capture: contract.
 * When `hdr` is set and the pixel format is 10-bit, frames are captured in
 * HDR (canonical display tone mapping) as ITU-R BT.2100 PQ.
 */
API_AVAILABLE(macos(15.0))
@interface SCVideo: AVVideo <SCStreamOutput, SCStreamDelegate>

/**
 * @brief Capture in HDR (BT.2020 PQ) when the pixel format is 10-bit.
 */
@property (nonatomic, assign) BOOL hdr;

@end
