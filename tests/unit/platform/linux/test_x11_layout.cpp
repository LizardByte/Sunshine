/**
 * @file tests/unit/platform/linux/test_x11_layout.cpp
 * @brief Test X11 capture layout mapping in src/platform/linux/x11_layout.h.
 */
#ifdef __linux__

  // test includes
  #include "../../../tests_common.h"

  // local includes
  #include <src/platform/linux/x11_layout.h>

namespace {
  /**
   * @brief Canonical 24-bit BGRX, little-endian TrueColor visual.
   *
   * @return Visual properties used as the accepted baseline.
   */
  constexpr platf::x11::x11_visual_t canonical_visual() {
    return {
      .depth = 24,
      .bits_per_pixel = 32,
      .little_endian = true,
      .true_color = true,
      .red_mask = 0xFF0000,
      .green_mask = 0xFF00,
      .blue_mask = 0xFF,
    };
  }
}  // namespace

/**
 * @brief The canonical 24-bit BGRX layout is declared as packed BGR0.
 */
TEST(X11LayoutTests, CanonicalBgrxIsDeclaredBgr0) {
  const auto pix_fmt = platf::x11::layout_to_pix_fmt(canonical_visual());
  ASSERT_TRUE(pix_fmt.has_value());
  EXPECT_EQ(*pix_fmt, platf::pix_fmt_e::bgr0);
}

/**
 * @brief Depth-32 TrueColor visuals store the same BGRX bytes as depth 24 with
 *        an alpha byte that packed BGR0 ignores, and are declared BGR0 too.
 */
TEST(X11LayoutTests, Depth32TrueColorIsDeclaredBgr0) {
  auto visual = canonical_visual();
  visual.depth = 32;

  const auto pix_fmt = platf::x11::layout_to_pix_fmt(visual);
  ASSERT_TRUE(pix_fmt.has_value());
  EXPECT_EQ(*pix_fmt, platf::pix_fmt_e::bgr0);
}

/**
 * @brief Layouts that use 4 bytes per pixel but are not packed BGR0, or that
 *        cannot be represented safely, are rejected instead of being declared
 *        as BGR0.
 */
TEST(X11LayoutTests, AmbiguousLayoutsAreRejected) {
  auto visual = canonical_visual();

  // 30-bit visuals also use 4 bytes per pixel but are not packed BGR0.
  visual.depth = 30;
  visual.red_mask = 0x3FF00000;
  visual.green_mask = 0xFFC00;
  visual.blue_mask = 0x3FF;
  EXPECT_FALSE(platf::x11::layout_to_pix_fmt(visual).has_value());

  // DirectColor exposes the same masks but interprets pixels as colormap indices.
  visual = canonical_visual();
  visual.true_color = false;
  EXPECT_FALSE(platf::x11::layout_to_pix_fmt(visual).has_value());

  // Reversed channel order.
  visual = canonical_visual();
  visual.red_mask = 0xFF;
  visual.blue_mask = 0xFF0000;
  EXPECT_FALSE(platf::x11::layout_to_pix_fmt(visual).has_value());

  // Big-endian servers store the channels in the opposite byte order.
  visual = canonical_visual();
  visual.little_endian = false;
  EXPECT_FALSE(platf::x11::layout_to_pix_fmt(visual).has_value());

  // Sub-32bpp and 16-bit layouts are not representable.
  visual = canonical_visual();
  visual.bits_per_pixel = 24;
  EXPECT_FALSE(platf::x11::layout_to_pix_fmt(visual).has_value());

  visual = canonical_visual();
  visual.depth = 16;
  visual.bits_per_pixel = 16;
  visual.red_mask = 0xF800;
  visual.green_mask = 0x7E0;
  visual.blue_mask = 0x1F;
  EXPECT_FALSE(platf::x11::layout_to_pix_fmt(visual).has_value());

  // Depth 32 is only supported with the canonical BGRX masks.
  visual = canonical_visual();
  visual.depth = 32;
  visual.red_mask = 0xFF;
  visual.blue_mask = 0xFF0000;
  EXPECT_FALSE(platf::x11::layout_to_pix_fmt(visual).has_value());
}

/**
 * @brief The storage size per pixel is looked up for the depth instead of
 *        being assumed: depth 24 may be stored in 24 or 32 bits per pixel.
 */
TEST(X11LayoutTests, PixmapFormatBitsPerPixel) {
  constexpr std::pair<int, int> formats[] = {
    {16, 16},
    {24, 32},
    {30, 32},
  };

  const auto bits_per_pixel_24 = platf::x11::pixmap_format_bits_per_pixel(formats, 24);
  ASSERT_TRUE(bits_per_pixel_24.has_value());
  EXPECT_EQ(*bits_per_pixel_24, 32);
  const auto bits_per_pixel_16 = platf::x11::pixmap_format_bits_per_pixel(formats, 16);
  ASSERT_TRUE(bits_per_pixel_16.has_value());
  EXPECT_EQ(*bits_per_pixel_16, 16);
  const auto bits_per_pixel_30 = platf::x11::pixmap_format_bits_per_pixel(formats, 30);
  ASSERT_TRUE(bits_per_pixel_30.has_value());
  EXPECT_EQ(*bits_per_pixel_30, 32);

  // When the server lists several formats for one depth, the first entry wins.
  constexpr std::pair<int, int> duplicate_depths[] = {{24, 32}, {24, 24}};
  const auto duplicate_bits_per_pixel = platf::x11::pixmap_format_bits_per_pixel(duplicate_depths, 24);
  ASSERT_TRUE(duplicate_bits_per_pixel.has_value());
  EXPECT_EQ(*duplicate_bits_per_pixel, 32);

  // A depth the server does not list cannot be captured.
  EXPECT_FALSE(platf::x11::pixmap_format_bits_per_pixel(formats, 8).has_value());

  // A 24-bit deep visual stored in 24 bits per pixel must not be declared BGR0.
  constexpr std::pair<int, int> packed24[] = {{24, 24}};
  auto visual = canonical_visual();
  const auto bits_per_pixel = platf::x11::pixmap_format_bits_per_pixel(packed24, 24);
  ASSERT_TRUE(bits_per_pixel.has_value());
  visual.bits_per_pixel = *bits_per_pixel;
  EXPECT_EQ(visual.bits_per_pixel, 24);
  EXPECT_FALSE(platf::x11::layout_to_pix_fmt(visual).has_value());
}

#endif
