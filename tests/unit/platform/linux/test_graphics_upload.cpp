/**
 * @file tests/unit/platform/linux/test_graphics_upload.cpp
 * @brief Test the packed image upload layout in src/platform/linux/unpack_layout.h.
 */
#ifdef __linux__

  // test includes
  #include "../../../tests_common.h"

  // local includes
  #include <src/platform/linux/unpack_layout.h>

/**
 * @brief A tightly packed image needs no unpack row length.
 */
TEST(GraphicsUploadTests, TightlyPackedRows) {
  const auto row_length = egl::classify_unpack_layout(100, 400);
  ASSERT_TRUE(row_length.has_value());
  EXPECT_EQ(*row_length, 0);
}

/**
 * @brief A padded stride that is a whole number of pixels is described
 *        directly to OpenGL through GL_UNPACK_ROW_LENGTH.
 */
TEST(GraphicsUploadTests, PaddedRowsUseUnpackRowLength) {
  const auto row_length = egl::classify_unpack_layout(100, 448);
  ASSERT_TRUE(row_length.has_value());
  EXPECT_EQ(*row_length, 112);
}

/**
 * @brief A stride that is not a whole number of pixels cannot be described by
 *        GL_UNPACK_ROW_LENGTH and is rejected instead of being sampled at the
 *        wrong offsets.
 */
TEST(GraphicsUploadTests, SubPixelStrideIsRejected) {
  EXPECT_FALSE(egl::classify_unpack_layout(100, 450).has_value());
}

/**
 * @brief A stride smaller than one row cannot be uploaded safely.
 */
TEST(GraphicsUploadTests, ShortStrideIsRejected) {
  EXPECT_FALSE(egl::classify_unpack_layout(100, 399).has_value());
  EXPECT_FALSE(egl::classify_unpack_layout(0, 0).has_value());

  // A width whose 32-bit byte count would overflow is still rejected safely.
  EXPECT_FALSE(egl::classify_unpack_layout(0x7fffffff, 0).has_value());
}

/**
 * @brief A packed 8-bit BGR capture is uploadable through the RAM path.
 */
TEST(GraphicsUploadTests, RamUploadAcceptsPackedBgr) {
  platf::img_t img {};
  img.width = 100;
  img.height = 50;
  img.row_pitch = 432;
  img.pixel_pitch = 4;
  img.pixel_format = platf::pix_fmt_e::bgra;

  const auto classification = egl::classify_ram_upload(img);
  ASSERT_EQ(classification.kind, egl::ram_upload_e::direct);
  EXPECT_EQ(classification.row_length, 108);
}

/**
 * @brief Declared formats the RAM upload cannot represent are rejected instead
 *        of being silently uploaded as packed BGR.
 */
TEST(GraphicsUploadTests, RamUploadRejectsUnsupportedFormats) {
  platf::img_t img {};
  img.width = 100;
  img.height = 50;
  img.row_pitch = 100;
  img.pixel_pitch = 1;
  img.pixel_format = platf::pix_fmt_e::nv12;

  EXPECT_EQ(egl::classify_ram_upload(img).kind, egl::ram_upload_e::unsupported_format);

  img.row_pitch = 400;
  img.pixel_pitch = 4;
  img.pixel_format = platf::pix_fmt_e::rgba;
  EXPECT_EQ(egl::classify_ram_upload(img).kind, egl::ram_upload_e::unsupported_format);

  // A declared packed BGR format whose reported size is not 4 bytes per pixel
  // would be read at 4 bytes per pixel and is rejected instead.
  img.pixel_format = platf::pix_fmt_e::bgr0;
  img.pixel_pitch = 3;
  img.row_pitch = 400;
  EXPECT_EQ(egl::classify_ram_upload(img).kind, egl::ram_upload_e::unsupported_format);
}

/**
 * @brief A stride that cannot describe a complete packed row is rejected.
 */
TEST(GraphicsUploadTests, RamUploadRejectsUnsupportedStride) {
  platf::img_t img {};
  img.width = 100;
  img.height = 50;
  img.row_pitch = 450;
  img.pixel_pitch = 4;
  img.pixel_format = platf::pix_fmt_e::bgr0;

  EXPECT_EQ(egl::classify_ram_upload(img).kind, egl::ram_upload_e::unsupported_stride);
}

#endif
