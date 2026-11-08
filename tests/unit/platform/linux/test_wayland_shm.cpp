/**
 * @file tests/unit/platform/linux/test_wayland_shm.cpp
 * @brief Test shared-memory Wayland capture buffers.
 */
#ifdef SUNSHINE_BUILD_WAYLAND
  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <array>
  #include <cstdint>
  #include <limits>

  // local includes
  #include <src/platform/linux/wayland.h>

TEST(WaylandShmTest, ValidatesProtocolSizeAndStrideBeforeAllocation) {
  EXPECT_EQ(wl::shm_buffer_size(WL_SHM_FORMAT_XRGB8888, 2, 2, 12), 24);
  EXPECT_EQ(wl::shm_buffer_size(WL_SHM_FORMAT_RGB888, 2, 2, 8), 16);
  EXPECT_FALSE(wl::shm_buffer_size(WL_SHM_FORMAT_XRGB8888, 0, 2, 8));
  EXPECT_FALSE(wl::shm_buffer_size(WL_SHM_FORMAT_XRGB8888, 2, 0, 8));
  EXPECT_FALSE(wl::shm_buffer_size(WL_SHM_FORMAT_XRGB8888, 2, 2, 7));
  EXPECT_FALSE(wl::shm_buffer_size(WL_SHM_FORMAT_RGB888, 2, 2, 5));
  EXPECT_FALSE(wl::shm_buffer_size(WL_SHM_FORMAT_XRGB8888, 1, 16384, 131072));
  EXPECT_FALSE(wl::shm_buffer_size(WL_SHM_FORMAT_XRGB8888, std::numeric_limits<std::uint32_t>::max(), 1, 4));
  EXPECT_FALSE(wl::shm_buffer_size(WL_SHM_FORMAT_RGB565, 2, 2, 4));
  EXPECT_EQ(wl::shm_buffer_size(WL_SHM_FORMAT_RGB888, 1, 1, std::numeric_limits<std::int32_t>::max()), std::numeric_limits<std::int32_t>::max());
}

TEST(WaylandShmTest, ConvertsSupportedFormatsWithPaddingAndInversion) {
  struct format_case_t {
    std::uint32_t format;
    std::array<std::uint8_t, 4> source;
    int bpp;
  };

  const std::array cases {
    format_case_t {WL_SHM_FORMAT_XRGB8888, {10, 20, 30, 255}, 4},
    format_case_t {WL_SHM_FORMAT_ARGB8888, {10, 20, 30, 255}, 4},
    format_case_t {WL_SHM_FORMAT_XBGR8888, {30, 20, 10, 255}, 4},
    format_case_t {WL_SHM_FORMAT_ABGR8888, {30, 20, 10, 255}, 4},
    format_case_t {WL_SHM_FORMAT_BGR888, {30, 20, 10, 0}, 3},
    format_case_t {WL_SHM_FORMAT_RGB888, {10, 20, 30, 0}, 3},
  };
  for (const auto &entry : cases) {
    for (bool inverted : {false, true}) {
      SCOPED_TRACE(entry.format);
      SCOPED_TRACE(inverted);
      wl::frame_t frame;
      frame.sd.width = 1;
      frame.sd.height = 2;
      frame.shm_format = entry.format;
      frame.shm_stride = 8;
      frame.shm_y_invert = inverted;
      frame.shm_data.assign(16, 0);
      for (int channel = 0; channel < entry.bpp; ++channel) {
        frame.shm_data[channel] = entry.source[channel];
        frame.shm_data[8 + channel] = entry.source[channel];
      }
      frame.shm_data[8] = 70;
      std::array<std::uint8_t, 16> pixels;
      pixels.fill(0xcc);
      platf::img_t img;
      img.width = 1;
      img.height = 2;
      img.pixel_pitch = 4;
      img.row_pitch = 8;
      img.data = pixels.data();
      ASSERT_TRUE(wl::convert_shm_frame(frame, img));
      const int first_row = inverted ? 8 : 0;
      EXPECT_EQ(pixels[first_row], 10);
      EXPECT_EQ(pixels[first_row + 1], 20);
      EXPECT_EQ(pixels[first_row + 2], 30);
      EXPECT_EQ(pixels[first_row + 3], 255);
      for (int row = 0; row < 2; ++row) {
        for (int padding = 4; padding < 8; ++padding) {
          EXPECT_EQ(pixels[row * 8 + padding], 0xcc);
        }
      }
      if (entry.format == WL_SHM_FORMAT_XBGR8888 || entry.format == WL_SHM_FORMAT_ABGR8888 || entry.format == WL_SHM_FORMAT_BGR888) {
        EXPECT_NE(pixels[2], pixels[10]);
      } else {
        EXPECT_NE(pixels[0], pixels[8]);
      }
    }
  }
}

TEST(WaylandShmTest, RejectsTruncatedOrIncompatibleImages) {
  wl::frame_t frame;
  frame.sd.width = 2;
  frame.sd.height = 2;
  frame.shm_format = WL_SHM_FORMAT_XRGB8888;
  frame.shm_stride = 8;
  frame.shm_data.assign(15, 0);
  std::array<std::uint8_t, 16> pixels {};
  platf::img_t img;
  img.width = 2;
  img.height = 2;
  img.pixel_pitch = 4;
  img.row_pitch = 8;
  img.data = pixels.data();
  EXPECT_FALSE(wl::convert_shm_frame(frame, img));
  frame.shm_data.resize(16);
  img.row_pitch = 7;
  EXPECT_FALSE(wl::convert_shm_frame(frame, img));
  img.row_pitch = 8;
  img.height = 3;
  EXPECT_FALSE(wl::convert_shm_frame(frame, img));
  img.height = 2;
  img.data = nullptr;
  EXPECT_FALSE(wl::convert_shm_frame(frame, img));
}
#endif
