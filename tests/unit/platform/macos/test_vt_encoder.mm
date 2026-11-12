/**
 * @file tests/unit/platform/macos/test_vt_encoder.mm
 * @brief Unit tests for src/platform/macos/vt_encoder.*.
 */
// Only compile these tests on macOS
#ifdef __APPLE__
  // test includes
  #include "../../../tests_common.h"

  // platform includes
  #import <CoreMedia/CoreMedia.h>

  // local includes
  #include <src/platform/macos/vt_encoder.h>

TEST(VTEncoder, H264ExpectedFrameRateIsCappedAtLevel52) {
  // 3840x2160 is 32400 macroblocks; level 5.2 allows 2073600 per second.
  EXPECT_EQ(platf::vt::expected_frame_rate(kCMVideoCodecType_H264, 3840, 2160, 120), 64);
  EXPECT_EQ(platf::vt::expected_frame_rate(kCMVideoCodecType_H264, 3840, 2160, 90), 64);
  // 2560x1440 is 14400 macroblocks.
  EXPECT_EQ(platf::vt::expected_frame_rate(kCMVideoCodecType_H264, 2560, 1440, 240), 144);
}

TEST(VTEncoder, H264ExpectedFrameRateWithinLevel52IsUnchanged) {
  EXPECT_EQ(platf::vt::expected_frame_rate(kCMVideoCodecType_H264, 3840, 2160, 60), 60);
  EXPECT_EQ(platf::vt::expected_frame_rate(kCMVideoCodecType_H264, 1920, 1080, 120), 120);
  // Partial macroblocks round up: 1920x1080 is 120x68 macroblocks.
  EXPECT_EQ(platf::vt::expected_frame_rate(kCMVideoCodecType_H264, 1920, 1080, 300), 254);
}

TEST(VTEncoder, OtherCodecsAndInvalidSizesAreNotCapped) {
  EXPECT_EQ(platf::vt::expected_frame_rate(kCMVideoCodecType_HEVC, 3840, 2160, 120), 120);
  EXPECT_EQ(platf::vt::expected_frame_rate(kCMVideoCodecType_H264, 0, 2160, 120), 120);
}
#endif
