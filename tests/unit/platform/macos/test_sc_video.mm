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
  #import <Metal/Metal.h>

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

  /**
   * @brief Fill a 10-bit 4:2:0 buffer with one flat Y'CbCr color.
   *
   * @param buffer Buffer to fill.
   * @param y Luma code value (10-bit, limited range).
   * @param cb Cb code value.
   * @param cr Cr code value.
   */
  void fill_flat(CVPixelBufferRef buffer, uint16_t y, uint16_t cb, uint16_t cr) {
    CVPixelBufferLockBaseAddress(buffer, 0);
    for (size_t row = 0; row < CVPixelBufferGetHeightOfPlane(buffer, 0); row++) {
      auto *luma = reinterpret_cast<uint16_t *>(static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(buffer, 0)) + row * CVPixelBufferGetBytesPerRowOfPlane(buffer, 0));
      for (size_t x = 0; x < CVPixelBufferGetWidthOfPlane(buffer, 0); x++) {
        luma[x] = static_cast<uint16_t>(y << 6);
      }
    }
    for (size_t row = 0; row < CVPixelBufferGetHeightOfPlane(buffer, 1); row++) {
      auto *chroma = reinterpret_cast<uint16_t *>(static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(buffer, 1)) + row * CVPixelBufferGetBytesPerRowOfPlane(buffer, 1));
      for (size_t x = 0; x < CVPixelBufferGetWidthOfPlane(buffer, 1); x++) {
        chroma[x * 2] = static_cast<uint16_t>(cb << 6);
        chroma[x * 2 + 1] = static_cast<uint16_t>(cr << 6);
      }
    }
    CVPixelBufferUnlockBaseAddress(buffer, 0);
  }

  /**
   * @brief Read the 10-bit luma and chroma code values at the top-left of a buffer.
   *
   * @param buffer Buffer to read.
   * @param y Receives the luma code value.
   * @param cb Receives the Cb code value.
   * @param cr Receives the Cr code value.
   */
  void read_codes(CVPixelBufferRef buffer, uint16_t &y, uint16_t &cb, uint16_t &cr) {
    CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    y = static_cast<const uint16_t *>(CVPixelBufferGetBaseAddressOfPlane(buffer, 0))[0] >> 6;
    const auto *chroma = static_cast<const uint16_t *>(CVPixelBufferGetBaseAddressOfPlane(buffer, 1));
    cb = chroma[0] >> 6;
    cr = chroma[1] >> 6;
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
  }
}  // namespace

TEST(MacosEdrHdrMetadataTest, SdrDisplayReportsNoMetadata) {
  SS_HDR_METADATA metadata;
  std::memset(&metadata, 0xff, sizeof(metadata));
  EXPECT_FALSE(platf::edr_hdr_metadata(1.0, 203.0, metadata));
  EXPECT_EQ(metadata.maxDisplayLuminance, 0);
  EXPECT_EQ(metadata.displayPrimaries[0].x, 0);
}

TEST(MacosEdrHdrMetadataTest, EdrHeadroomSetsPeakOverStreamedSdrWhite) {
  SS_HDR_METADATA metadata {};
  ASSERT_TRUE(platf::edr_hdr_metadata(5.0, 203.0, metadata));
  EXPECT_EQ(metadata.maxDisplayLuminance, 1015);  // 5 x 203-nit SDR white
  EXPECT_EQ(metadata.maxContentLightLevel, 1015);
  EXPECT_EQ(metadata.maxFrameAverageLightLevel, 1015);
  EXPECT_EQ(metadata.maxFullFrameLuminance, 1015);
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

TEST(MacosEdrHdrMetadataTest, PeakFollowsConfiguredSdrWhite) {
  SS_HDR_METADATA metadata {};
  ASSERT_TRUE(platf::edr_hdr_metadata(5.0, 480.0, metadata));
  EXPECT_EQ(metadata.maxDisplayLuminance, 2400);
}

TEST(MacosEdrHdrMetadataTest, PeakIsClampedToPqMaximum) {
  SS_HDR_METADATA metadata {};
  ASSERT_TRUE(platf::edr_hdr_metadata(100.0, 203.0, metadata));
  EXPECT_EQ(metadata.maxDisplayLuminance, 10000);
}

TEST(MacosScVideoTest, CopyKeepsPixelsColorTagsAndTiming) {
  CVPixelBufferRef source = make_pq_buffer(64, 32, 3);
  CMSampleBufferRef sample = make_sample(source, 42);
  CVPixelBufferPoolRef pool = nullptr;

  CMSampleBufferRef copy = sc_copy_sample_buffer(sample, &pool, 1.0f);
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
  CMSampleBufferRef small_copy = sc_copy_sample_buffer(small_sample, &pool, 1.0f);
  ASSERT_NE(small_copy, nullptr);

  CVPixelBufferRef large = make_pq_buffer(128, 64, 2);
  CMSampleBufferRef large_sample = make_sample(large, 2);
  CMSampleBufferRef large_copy = sc_copy_sample_buffer(large_sample, &pool, 1.0f);
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

TEST(MacosScVideoTest, PqGainLiftsSdrWhiteTo203Nits) {
  if (!MTLCreateSystemDefaultDevice()) {
    GTEST_SKIP() << "Metal is not available";
  }
  CVPixelBufferRef source = make_pq_buffer(64, 32, 0);
  fill_flat(source, 509, 512, 512);  // 100-nit grey: ScreenCaptureKit's SDR white
  CMSampleBufferRef sample = make_sample(source, 7);
  CVPixelBufferPoolRef pool = nullptr;

  CMSampleBufferRef copy = sc_copy_sample_buffer(sample, &pool, 2.03f);
  ASSERT_NE(copy, nullptr);
  uint16_t y = 0, cb = 0, cr = 0;
  read_codes(CMSampleBufferGetImageBuffer(copy), y, cb, cr);
  EXPECT_NEAR(y, 573, 1);  // PQ code for 203 nits
  EXPECT_NEAR(cb, 512, 1);
  EXPECT_NEAR(cr, 512, 1);

  CFRelease(copy);
  CFRelease(sample);
  CVPixelBufferRelease(source);
  CVPixelBufferPoolRelease(pool);
}

TEST(MacosScVideoTest, PqGainLeavesNonPqFramesUntouched) {
  CVPixelBufferRef source = make_pq_buffer(64, 32, 5);
  CVBufferRemoveAttachment(source, kCVImageBufferTransferFunctionKey);
  CMSampleBufferRef sample = make_sample(source, 3);
  CVPixelBufferPoolRef pool = nullptr;

  CMSampleBufferRef copy = sc_copy_sample_buffer(sample, &pool, 2.03f);
  ASSERT_NE(copy, nullptr);
  EXPECT_TRUE(same_pixels(source, CMSampleBufferGetImageBuffer(copy)));

  CFRelease(copy);
  CFRelease(sample);
  CVPixelBufferRelease(source);
  CVPixelBufferPoolRelease(pool);
}

#endif
