/**
 * @file tests/unit/platform/macos/test_sc_video.mm
 * @brief Unit tests for src/platform/macos/sc_video.* and the macOS HDR metadata helper.
 */

// Only compile these tests on macOS
#ifdef __APPLE__

  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <algorithm>
  #include <cstring>

  // platform includes
  #import <CoreMedia/CoreMedia.h>
  #import <CoreVideo/CoreVideo.h>

  // local includes
  #include <src/platform/macos/misc.h>
  #import <src/platform/macos/sc_video.h>

namespace {
  /**
   * @brief Create an IOSurface-backed 10-bit 4:2:0 buffer tagged as BT.2020 PQ and filled with a pattern.
   *
   * @param width Buffer width in pixels.
   * @param height Buffer height in pixels.
   * @param seed Value mixed into the pattern.
   * @return Retained pixel buffer.
   */
  CVPixelBufferRef make_pq_buffer(size_t width, size_t height, uint8_t seed) {
    NSDictionary *attrs = @{(id) kCVPixelBufferIOSurfacePropertiesKey: @ {}};
    CVPixelBufferRef buffer = nullptr;
    EXPECT_EQ(CVPixelBufferCreate(kCFAllocatorDefault, width, height, kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange, (__bridge CFDictionaryRef) attrs, &buffer), kCVReturnSuccess);

    CVPixelBufferLockBaseAddress(buffer, 0);
    for (size_t plane = 0; plane < CVPixelBufferGetPlaneCount(buffer); plane++) {
      auto *data = static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(buffer, plane));
      const size_t bytes = CVPixelBufferGetBytesPerRowOfPlane(buffer, plane) * CVPixelBufferGetHeightOfPlane(buffer, plane);
      for (size_t i = 0; i < bytes; i++) {
        data[i] = static_cast<uint8_t>(i * 7 + plane * 31 + seed);
      }
    }
    CVPixelBufferUnlockBaseAddress(buffer, 0);

    CVBufferSetAttachment(buffer, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_2020, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(buffer, kCVImageBufferTransferFunctionKey, kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(buffer, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_2020, kCVAttachmentMode_ShouldPropagate);
    return buffer;
  }

  /**
   * @brief Wrap a pixel buffer in a sample buffer with the given presentation time.
   *
   * @param buffer Pixel buffer to wrap.
   * @param pts_value Presentation time in 1/120 s units.
   * @return Retained sample buffer.
   */
  CMSampleBufferRef make_sample(CVPixelBufferRef buffer, int64_t pts_value) {
    CMVideoFormatDescriptionRef desc = nullptr;
    EXPECT_EQ(CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, buffer, &desc), noErr);
    CMSampleTimingInfo timing {CMTimeMake(1, 120), CMTimeMake(pts_value, 120), kCMTimeInvalid};
    CMSampleBufferRef sample = nullptr;
    EXPECT_EQ(CMSampleBufferCreateReadyWithImageBuffer(kCFAllocatorDefault, buffer, desc, &timing, &sample), noErr);
    CFRelease(desc);
    return sample;
  }

  /**
   * @brief Compare the visible bytes of every plane of two 4:2:0 buffers.
   *
   * @param a First buffer.
   * @param b Second buffer.
   * @return True when all rows match.
   */
  bool same_pixels(CVPixelBufferRef a, CVPixelBufferRef b) {
    bool same = true;
    CVPixelBufferLockBaseAddress(a, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferLockBaseAddress(b, kCVPixelBufferLock_ReadOnly);
    for (size_t plane = 0; plane < CVPixelBufferGetPlaneCount(a) && same; plane++) {
      const auto *pa = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(a, plane));
      const auto *pb = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(b, plane));
      // Y has one 16-bit sample per pixel, CbCr two.
      const size_t row_bytes = CVPixelBufferGetWidthOfPlane(a, plane) * (plane == 0 ? 2 : 4);
      for (size_t row = 0; row < CVPixelBufferGetHeightOfPlane(a, plane) && same; row++) {
        same = std::memcmp(pa + row * CVPixelBufferGetBytesPerRowOfPlane(a, plane), pb + row * CVPixelBufferGetBytesPerRowOfPlane(b, plane), row_bytes) == 0;
      }
    }
    CVPixelBufferUnlockBaseAddress(b, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferUnlockBaseAddress(a, kCVPixelBufferLock_ReadOnly);
    return same;
  }
}  // namespace

TEST(MacosEdrHdrMetadataTest, SdrDisplayReportsNoMetadata) {
  SS_HDR_METADATA metadata;
  std::memset(&metadata, 0xff, sizeof(metadata));
  EXPECT_FALSE(platf::edr_hdr_metadata(1.0, metadata));
  EXPECT_EQ(metadata.maxDisplayLuminance, 0);
  EXPECT_EQ(metadata.displayPrimaries[0].x, 0);
}

TEST(MacosEdrHdrMetadataTest, EdrHeadroomSetsPeakOverReferenceWhite) {
  SS_HDR_METADATA metadata {};
  ASSERT_TRUE(platf::edr_hdr_metadata(5.0, metadata));
  EXPECT_EQ(metadata.maxDisplayLuminance, 500);
  EXPECT_EQ(metadata.maxContentLightLevel, 500);
  EXPECT_EQ(metadata.maxFrameAverageLightLevel, 500);
  EXPECT_EQ(metadata.maxFullFrameLuminance, 500);
  EXPECT_EQ(metadata.minDisplayLuminance, 50);
  EXPECT_EQ(metadata.displayPrimaries[0].x, 32000);
  EXPECT_EQ(metadata.displayPrimaries[0].y, 16500);
  EXPECT_EQ(metadata.displayPrimaries[1].x, 15000);
  EXPECT_EQ(metadata.displayPrimaries[1].y, 30000);
  EXPECT_EQ(metadata.displayPrimaries[2].x, 7500);
  EXPECT_EQ(metadata.displayPrimaries[2].y, 3000);
  EXPECT_EQ(metadata.whitePoint.x, 15635);
  EXPECT_EQ(metadata.whitePoint.y, 16450);
}

TEST(MacosEdrHdrMetadataTest, PeakIsClampedToPqMaximum) {
  SS_HDR_METADATA metadata {};
  ASSERT_TRUE(platf::edr_hdr_metadata(250.0, metadata));
  EXPECT_EQ(metadata.maxDisplayLuminance, 10000);
}

TEST(MacosScVideoTest, CopyKeepsPixelsColorTagsAndTiming) {
  CVPixelBufferRef source = make_pq_buffer(64, 32, 3);
  CMSampleBufferRef sample = make_sample(source, 42);
  CVPixelBufferPoolRef pool = nullptr;

  CMSampleBufferRef copy = sc_copy_sample_buffer(sample, &pool);
  ASSERT_NE(copy, nullptr);
  ASSERT_NE(pool, nullptr);

  CVPixelBufferRef copied = CMSampleBufferGetImageBuffer(copy);
  EXPECT_NE(copied, source);
  EXPECT_EQ(CVPixelBufferGetPixelFormatType(copied), kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange);
  EXPECT_EQ(CVPixelBufferGetWidth(copied), 64u);
  EXPECT_EQ(CVPixelBufferGetHeight(copied), 32u);
  EXPECT_TRUE(same_pixels(source, copied));
  id transfer = CFBridgingRelease(CVBufferCopyAttachment(copied, kCVImageBufferTransferFunctionKey, nullptr));
  id primaries = CFBridgingRelease(CVBufferCopyAttachment(copied, kCVImageBufferColorPrimariesKey, nullptr));
  EXPECT_TRUE([transfer isEqual:(__bridge id) kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ]);
  EXPECT_TRUE([primaries isEqual:(__bridge id) kCVImageBufferColorPrimaries_ITU_R_2020]);
  EXPECT_EQ(CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(copy), CMTimeMake(42, 120)), 0);
  EXPECT_NE(CVPixelBufferGetIOSurface(copied), nullptr);

  CFRelease(copy);
  CFRelease(sample);
  CVPixelBufferRelease(source);
  CVPixelBufferPoolRelease(pool);
}

TEST(MacosScVideoTest, PoolIsRecreatedWhenFrameSizeChanges) {
  CVPixelBufferPoolRef pool = nullptr;

  CVPixelBufferRef small = make_pq_buffer(64, 32, 1);
  CMSampleBufferRef small_sample = make_sample(small, 1);
  CMSampleBufferRef small_copy = sc_copy_sample_buffer(small_sample, &pool);
  ASSERT_NE(small_copy, nullptr);

  CVPixelBufferRef large = make_pq_buffer(128, 64, 2);
  CMSampleBufferRef large_sample = make_sample(large, 2);
  CMSampleBufferRef large_copy = sc_copy_sample_buffer(large_sample, &pool);
  ASSERT_NE(large_copy, nullptr);
  NSDictionary *pool_attrs = (__bridge NSDictionary *) CVPixelBufferPoolGetPixelBufferAttributes(pool);
  EXPECT_EQ([pool_attrs[(id) kCVPixelBufferWidthKey] unsignedLongValue], 128u);
  EXPECT_EQ(CVPixelBufferGetWidth(CMSampleBufferGetImageBuffer(large_copy)), 128u);
  EXPECT_TRUE(same_pixels(large, CMSampleBufferGetImageBuffer(large_copy)));

  CFRelease(small_copy);
  CFRelease(large_copy);
  CFRelease(small_sample);
  CFRelease(large_sample);
  CVPixelBufferRelease(small);
  CVPixelBufferRelease(large);
  CVPixelBufferPoolRelease(pool);
}

#endif
