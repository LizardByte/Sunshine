/**
 * @file tests/unit/platform/test_common.cpp
 * @brief Test src/platform/common.*.
 */

// test includes
#include "../../tests_common.h"

// lib includes
#include <boost/asio/ip/host_name.hpp>

// local includes
#include <src/platform/common.h>

TEST(HostnameTests, TestAsioEquality) {
  // These should be equivalent on all platforms for ASCII hostnames
  ASSERT_EQ(platf::get_host_name(), boost::asio::ip::host_name());
}

/**
 * @brief Capture formats representable as packed 8-bit BGR are accepted by the RAM
 *        conversion paths, while other declared formats are rejected.
 */
TEST(PixelFormatTests, IsBgrCaptureFormat) {
  EXPECT_TRUE(platf::is_bgr_capture_format(platf::pix_fmt_e::unknown));
  EXPECT_TRUE(platf::is_bgr_capture_format(platf::pix_fmt_e::bgr0));
  EXPECT_TRUE(platf::is_bgr_capture_format(platf::pix_fmt_e::bgra));

  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::nv12));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::p010));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::yuv420p));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::yuv420p10));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::ayuv));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::yuv444p));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::yuv444p16));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::y410));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::xbgr2101010));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::bgra1010102));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::rgba1010102));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::abgr2101010));
  EXPECT_FALSE(platf::is_bgr_capture_format(platf::pix_fmt_e::argb2101010));
}

TEST(PixelFormatTests, FromPixFmt) {
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::yuv420p), "yuv420p");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::yuv420p10), "yuv420p10");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::nv12), "nv12");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::p010), "p010");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::ayuv), "ayuv");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::yuv444p16), "yuv444p16");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::yuv444p), "yuv444p");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::y410), "y410");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::bgr0), "bgr0");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::bgra), "bgra");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::xbgr2101010), "xbgr2101010");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::bgra1010102), "bgra1010102");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::rgba1010102), "rgba1010102");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::abgr2101010), "abgr2101010");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::argb2101010), "argb2101010");
  EXPECT_EQ(platf::from_pix_fmt(platf::pix_fmt_e::unknown), "unknown");
}
