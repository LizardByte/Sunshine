/**
 * @file tests/unit/platform/linux/test_cuda_viewport.cpp
 * @brief Test where the CUDA converter places a captured image in the output frame.
 */
#ifdef SUNSHINE_BUILD_CUDA
  // test includes
  #include "../../../tests_common.h"

  // local includes
  #include <src/platform/linux/cuda.h>

TEST(CudaViewportTest, OddSideBarsAreRoundedDownToEven) {
  float scale {};
  // 2344x1080 in a 1744x800 frame leaves 3.85 px bars at each side; an odd offset of 3 swapped U and V.
  const auto viewport = cuda::make_viewport(2344, 1080, 1744, 800, scale);

  EXPECT_EQ(viewport.offsetX, 2);
  EXPECT_EQ(viewport.offsetY, 0);
  EXPECT_EQ(viewport.width, 1736);
  EXPECT_EQ(viewport.height, 800);
  EXPECT_LE(viewport.offsetX + viewport.width, 1744);
  EXPECT_FLOAT_EQ(scale, 1.35f);
}

TEST(CudaViewportTest, OddTopAndBottomBarsAreRoundedDownToEven) {
  float scale {};
  const auto viewport = cuda::make_viewport(2344, 1080, 1744, 810, scale);

  EXPECT_EQ(viewport.offsetX, 0);
  EXPECT_EQ(viewport.offsetY, 2);
  EXPECT_EQ(viewport.width, 1744);
  EXPECT_EQ(viewport.height, 803);
  EXPECT_LE(viewport.offsetY + viewport.height, 810);
}

TEST(CudaViewportTest, EvenSideBarsAreKept) {
  float scale {};
  const auto viewport = cuda::make_viewport(2344, 1080, 1776, 816, scale);

  EXPECT_EQ(viewport.offsetX, 2);
  EXPECT_EQ(viewport.offsetY, 0);
}

TEST(CudaViewportTest, MatchingAspectRatioHasNoBars) {
  float scale {};
  const auto viewport = cuda::make_viewport(1920, 1080, 1280, 720, scale);

  EXPECT_EQ(viewport.offsetX, 0);
  EXPECT_EQ(viewport.offsetY, 0);
  EXPECT_EQ(viewport.width, 1280);
  EXPECT_EQ(viewport.height, 720);
  EXPECT_FLOAT_EQ(scale, 1.5f);
}

#endif
