/**
 * @file tests/unit/platform/linux/test_graphics.cpp
 * @brief Test the chroma_supersample integer-factor detection.
 */
#if defined(__linux__)
  #include "../../../tests_common.h"

  #include <src/platform/linux/graphics.h>

// The exactness property chroma_supersample relies on only holds for an exact, even
// integer upscale in both axes - see src/platform/linux/graphics.cpp for the derivation.
// These cases mirror the values verified against a numpy simulation of the GL sampling
// rules during development: 2x/4x/6x/8x/10x/12x are bit-exact, 3x/5x/7x are not (and are
// measurably worse than the existing bilinear path), so supersample_factor() must reject
// odd factors rather than merely treat them as "less exact."
TEST(SupersampleFactorTest, AcceptsEvenIntegerFactors) {
  EXPECT_EQ(egl::supersample_factor(1920, 1080, 3840, 2160), 2);
  EXPECT_EQ(egl::supersample_factor(1920, 1080, 7680, 4320), 4);
  EXPECT_EQ(egl::supersample_factor(480, 270, 2880, 1620), 6);
  EXPECT_EQ(egl::supersample_factor(480, 270, 3840, 2160), 8);
  EXPECT_EQ(egl::supersample_factor(480, 270, 4800, 2700), 10);
  EXPECT_EQ(egl::supersample_factor(480, 270, 5760, 3240), 12);
}

TEST(SupersampleFactorTest, RejectsOddFactors) {
  EXPECT_EQ(egl::supersample_factor(1920, 1080, 5760, 3240), 0);  // 3x
  EXPECT_EQ(egl::supersample_factor(480, 270, 2400, 1350), 0);  // 5x
  EXPECT_EQ(egl::supersample_factor(480, 270, 3360, 1890), 0);  // 7x
}

TEST(SupersampleFactorTest, RejectsBelowMinimumFactor) {
  // Same resolution (1x) and any downscale must not enable point sampling.
  EXPECT_EQ(egl::supersample_factor(1920, 1080, 1920, 1080), 0);
  EXPECT_EQ(egl::supersample_factor(1920, 1080, 1280, 720), 0);
}

TEST(SupersampleFactorTest, RejectsNonIntegerRatio) {
  // 1.5x - not an integer multiple in either axis.
  EXPECT_EQ(egl::supersample_factor(1920, 1080, 2880, 1620), 0);
}

TEST(SupersampleFactorTest, RejectsAnisotropicRatio) {
  // Width scales 2x, height scales 3x - axes disagree, so no uniform block replication.
  EXPECT_EQ(egl::supersample_factor(1920, 1080, 3840, 3240), 0);
}

TEST(SupersampleFactorTest, RejectsNonPositiveCaptureSize) {
  EXPECT_EQ(egl::supersample_factor(0, 1080, 3840, 2160), 0);
  EXPECT_EQ(egl::supersample_factor(1920, 0, 3840, 2160), 0);
  EXPECT_EQ(egl::supersample_factor(-1920, 1080, 3840, 2160), 0);
}
#endif
