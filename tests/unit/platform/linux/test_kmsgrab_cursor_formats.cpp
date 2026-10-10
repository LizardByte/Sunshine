/**
 * @file tests/unit/platform/linux/test_kmsgrab_cursor_formats.cpp
 * @brief Test KMS cursor format conversion and validation.
 */
#include <array>
#include <cstdint>
#include <gtest/gtest.h>

#ifdef SUNSHINE_BUILD_DRM
  #include <drm_fourcc.h>

namespace platf::kms {

  bool is_cursor_32bpp_format(uint32_t fmt);
  bool convert_pixel_to_argb8888(std::uint8_t *p, std::uint32_t fmt, bool x_is_alpha = false);

}  // namespace platf::kms

namespace {

  TEST(KmsGrabCursor, AcceptsAllSupported32bppFormats) {
    EXPECT_TRUE(platf::kms::is_cursor_32bpp_format(DRM_FORMAT_ARGB8888));
    EXPECT_TRUE(platf::kms::is_cursor_32bpp_format(DRM_FORMAT_ABGR8888));
    EXPECT_TRUE(platf::kms::is_cursor_32bpp_format(DRM_FORMAT_RGBA8888));
    EXPECT_TRUE(platf::kms::is_cursor_32bpp_format(DRM_FORMAT_BGRA8888));
    EXPECT_TRUE(platf::kms::is_cursor_32bpp_format(DRM_FORMAT_XRGB8888));
    EXPECT_TRUE(platf::kms::is_cursor_32bpp_format(DRM_FORMAT_XBGR8888));
    EXPECT_TRUE(platf::kms::is_cursor_32bpp_format(DRM_FORMAT_RGBX8888));
    EXPECT_TRUE(platf::kms::is_cursor_32bpp_format(DRM_FORMAT_BGRX8888));

    EXPECT_FALSE(platf::kms::is_cursor_32bpp_format(DRM_FORMAT_NV12));
    EXPECT_FALSE(platf::kms::is_cursor_32bpp_format(DRM_FORMAT_YUYV));
  }

  TEST(KmsGrabCursor, ConvertsArgb8888LayoutToExpectedByteOrder) {
    std::array<std::uint8_t, 4> pixel = {0x11, 0x22, 0x33, 0x44};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_ARGB8888, false));

    EXPECT_EQ(pixel[0], 0x11);  // B
    EXPECT_EQ(pixel[1], 0x22);  // G
    EXPECT_EQ(pixel[2], 0x33);  // R
    EXPECT_EQ(pixel[3], 0x44);  // A
  }

  TEST(KmsGrabCursor, ConvertsAbgr8888LayoutToExpectedByteOrder) {
    std::array<std::uint8_t, 4> pixel = {0x11, 0x22, 0x33, 0x44};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_ABGR8888, false));

    EXPECT_EQ(pixel[0], 0x33);  // B
    EXPECT_EQ(pixel[1], 0x22);  // G
    EXPECT_EQ(pixel[2], 0x11);  // R
    EXPECT_EQ(pixel[3], 0x44);  // A
  }

  TEST(KmsGrabCursor, ConvertsRgba8888LayoutToExpectedByteOrder) {
    std::array<std::uint8_t, 4> pixel = {0x11, 0x22, 0x33, 0x44};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_RGBA8888, false));

    EXPECT_EQ(pixel[0], 0x22);  // B
    EXPECT_EQ(pixel[1], 0x33);  // G
    EXPECT_EQ(pixel[2], 0x44);  // R
    EXPECT_EQ(pixel[3], 0x11);  // A
  }

  TEST(KmsGrabCursor, ConvertsBgra8888LayoutToExpectedByteOrder) {
    std::array<std::uint8_t, 4> pixel = {0x11, 0x22, 0x33, 0x44};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_BGRA8888, false));

    EXPECT_EQ(pixel[0], 0x44);  // B
    EXPECT_EQ(pixel[1], 0x33);  // G
    EXPECT_EQ(pixel[2], 0x22);  // R
    EXPECT_EQ(pixel[3], 0x11);  // A
  }

  TEST(KmsGrabCursor, ConvertsXrgb8888WithXChannelAlphaMode) {
    std::array<std::uint8_t, 4> pixel = {0x10, 0x20, 0x30, 0x80};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_XRGB8888, true));

    EXPECT_EQ(pixel[0], 0x10);  // B
    EXPECT_EQ(pixel[1], 0x20);  // G
    EXPECT_EQ(pixel[2], 0x30);  // R
    EXPECT_EQ(pixel[3], 0x80);  // A from X byte
  }

  TEST(KmsGrabCursor, ConvertsXrgb8888WithoutXChannelAlphaMode) {
    std::array<std::uint8_t, 4> pixel = {0x10, 0x20, 0x30, 0x00};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_XRGB8888, false));

    EXPECT_EQ(pixel[0], 0x10);  // B
    EXPECT_EQ(pixel[1], 0x20);  // G
    EXPECT_EQ(pixel[2], 0x30);  // R
    EXPECT_EQ(pixel[3], 0xFF);  // Opaque
  }

  TEST(KmsGrabCursor, ConvertsXbgr8888WithXChannelAlphaMode) {
    std::array<std::uint8_t, 4> pixel = {0x80, 0x20, 0x10, 0x30};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_XBGR8888, true));

    EXPECT_EQ(pixel[0], 0x10);  // B
    EXPECT_EQ(pixel[1], 0x20);  // G
    EXPECT_EQ(pixel[2], 0x80);  // R
    EXPECT_EQ(pixel[3], 0x30);  // A from X byte
  }

  TEST(KmsGrabCursor, ConvertsRgbx8888WithXChannelAlphaMode) {
    std::array<std::uint8_t, 4> pixel = {0x11, 0x22, 0x33, 0x44};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_RGBX8888, true));

    EXPECT_EQ(pixel[0], 0x22);  // B
    EXPECT_EQ(pixel[1], 0x33);  // G
    EXPECT_EQ(pixel[2], 0x44);  // R
    EXPECT_EQ(pixel[3], 0x11);  // A from X byte
  }

  TEST(KmsGrabCursor, ConvertsBgrx8888WithXChannelAlphaMode) {
    std::array<std::uint8_t, 4> pixel = {0x11, 0x22, 0x33, 0x44};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_BGRX8888, true));

    EXPECT_EQ(pixel[0], 0x44);  // B
    EXPECT_EQ(pixel[1], 0x33);  // G
    EXPECT_EQ(pixel[2], 0x22);  // R
    EXPECT_EQ(pixel[3], 0x11);  // A from X byte
  }

  TEST(KmsGrabCursor, ConvertsXbgr8888WithoutXChannelAlphaMode) {
    std::array<std::uint8_t, 4> pixel = {0x80, 0x20, 0x10, 0x30};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_XBGR8888, false));

    EXPECT_EQ(pixel[0], 0x10);  // B
    EXPECT_EQ(pixel[1], 0x20);  // G
    EXPECT_EQ(pixel[2], 0x80);  // R
    EXPECT_EQ(pixel[3], 0xFF);  // Opaque
  }

  TEST(KmsGrabCursor, ConvertsRgbx8888WithoutXChannelAlphaMode) {
    std::array<std::uint8_t, 4> pixel = {0x11, 0x22, 0x33, 0x44};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_RGBX8888, false));

    EXPECT_EQ(pixel[0], 0x22);  // B
    EXPECT_EQ(pixel[1], 0x33);  // G
    EXPECT_EQ(pixel[2], 0x44);  // R
    EXPECT_EQ(pixel[3], 0xFF);  // Opaque
  }

  TEST(KmsGrabCursor, ConvertsBgrx8888WithoutXChannelAlphaMode) {
    std::array<std::uint8_t, 4> pixel = {0x11, 0x22, 0x33, 0x44};

    EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_BGRX8888, false));

    EXPECT_EQ(pixel[0], 0x44);  // B
    EXPECT_EQ(pixel[1], 0x33);  // G
    EXPECT_EQ(pixel[2], 0x22);  // R
    EXPECT_EQ(pixel[3], 0xFF);  // Opaque
  }

  TEST(KmsGrabCursor, HandlesTransparentPartialAndOpaqueAlphaValues) {
    {
      std::array<std::uint8_t, 4> pixel = {0x10, 0x20, 0x30, 0x00};
      EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_ARGB8888, false));
      EXPECT_EQ(pixel[3], 0x00);
    }

    {
      std::array<std::uint8_t, 4> pixel = {0x10, 0x20, 0x30, 0x80};
      EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_ARGB8888, false));
      EXPECT_EQ(pixel[3], 0x80);
    }

    {
      std::array<std::uint8_t, 4> pixel = {0x10, 0x20, 0x30, 0xFF};
      EXPECT_TRUE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_ARGB8888, false));
      EXPECT_EQ(pixel[3], 0xFF);
    }
  }

  TEST(KmsGrabCursor, RejectsUnsupportedFormats) {
    std::array<std::uint8_t, 4> pixel = {0x11, 0x22, 0x33, 0x44};

    EXPECT_FALSE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_NV12, false));
    EXPECT_FALSE(platf::kms::convert_pixel_to_argb8888(pixel.data(), DRM_FORMAT_YUYV, false));
  }

}  // namespace
#endif  // SUNSHINE_BUILD_DRM
