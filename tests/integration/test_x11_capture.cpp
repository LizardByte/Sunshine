/**
 * @file tests/integration/test_x11_capture.cpp
 * @brief Integration tests for the X11 capture backend against a real X server.
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_X11)

  // test includes
  #include "../tests_common.h"
  #include "x11_capture_helpers.h"
  #include "xvfb_server.h"

  // standard includes
  #include <array>
  #include <cstddef>
  #include <cstdint>
  #include <memory>
  #include <string>

  // platform includes
  #include <X11/Xlib.h>

  // local includes
  #include <src/platform/common.h>
  #include <src/video.h>

namespace platf {
  extern std::shared_ptr<display_t> x11_display(mem_type_e hwdevice_type, const std::string &display_name, const video::config_t &config);
}  // namespace platf

/**
 * @brief The X11 backend captures the virtual desktop and declares packed BGR0.
 */
TEST(X11IntegrationTests, CapturesDesktopAsDeclaredBgr0) {
  xvfb::xvfb_server_t server;
  const auto server_result = server.start();
  if (server_result == xvfb::start_result_e::unavailable) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "Xvfb is not available";
    GTEST_SKIP() << "Xvfb is not available";
  }
  ASSERT_EQ(server_result, xvfb::start_result_e::started) << "Xvfb failed to start";

  video::config_t config {};
  config.width = 1280;
  config.height = 720;
  config.framerate = 60;

  auto disp = platf::x11_display(platf::mem_type_e::system, "", config);
  if (!disp) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "X11 capture backend is not available";
    GTEST_SKIP() << "X11 capture backend is not available";
  }

  x11_test::xdisplay_ptr xdisplay(XOpenDisplay(nullptr), &XCloseDisplay);
  if (!xdisplay) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "Could not open the Xvfb display";
    GTEST_SKIP() << "Could not open the Xvfb display";
  }

  // 0x336699 is R=0x33, G=0x66, B=0x99 in the canonical TrueColor masks.
  x11_test::fill_root_window(xdisplay.get(), 0x336699);

  auto captured = x11_test::capture_one_frame(*disp);
  ASSERT_NE(captured, nullptr) << "X11 capture did not produce a frame";
  ASSERT_NE(captured->data, nullptr);

  EXPECT_EQ(captured->pixel_format, platf::pix_fmt_e::bgr0);
  EXPECT_EQ(captured->width, 1280);
  EXPECT_EQ(captured->height, 720);
  EXPECT_GE(captured->row_pitch, captured->width * 4);

  // Packed BGRX memory order: blue, green, red, padding.
  const auto *center = captured->data + static_cast<std::ptrdiff_t>((captured->height / 2) * captured->row_pitch + (captured->width / 2) * 4);
  EXPECT_EQ(center[0], 0x99) << "blue channel";
  EXPECT_EQ(center[1], 0x66) << "green channel";
  EXPECT_EQ(center[2], 0x33) << "red channel";
}

/**
 * @brief Later captures reflect content changes on the X server.
 */
TEST(X11IntegrationTests, CapturesUpdatedContent) {
  xvfb::xvfb_server_t server;
  const auto server_result = server.start(640, 480);
  if (server_result == xvfb::start_result_e::unavailable) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "Xvfb is not available";
    GTEST_SKIP() << "Xvfb is not available";
  }
  ASSERT_EQ(server_result, xvfb::start_result_e::started) << "Xvfb failed to start";

  video::config_t config {};
  config.width = 640;
  config.height = 480;
  config.framerate = 60;

  auto disp = platf::x11_display(platf::mem_type_e::system, "", config);
  if (!disp) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "X11 capture backend is not available";
    GTEST_SKIP() << "X11 capture backend is not available";
  }

  x11_test::xdisplay_ptr xdisplay(XOpenDisplay(nullptr), &XCloseDisplay);
  if (!xdisplay) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "Could not open the Xvfb display";
    GTEST_SKIP() << "Could not open the Xvfb display";
  }

  x11_test::fill_root_window(xdisplay.get(), 0xFF0000);
  auto red_frame = x11_test::capture_one_frame(*disp);
  ASSERT_NE(red_frame, nullptr);

  x11_test::fill_root_window(xdisplay.get(), 0x00FF00);
  auto green_frame = x11_test::capture_one_frame(*disp);
  ASSERT_NE(green_frame, nullptr);

  const auto sample_center = [](const std::shared_ptr<platf::img_t> &img) {
    const auto *center = img->data + static_cast<std::ptrdiff_t>((img->height / 2) * img->row_pitch + (img->width / 2) * 4);
    return std::array<std::uint8_t, 3> {center[0], center[1], center[2]};
  };

  EXPECT_EQ(sample_center(red_frame), (std::array<std::uint8_t, 3> {0x00, 0x00, 0xFF}));
  EXPECT_EQ(sample_center(green_frame), (std::array<std::uint8_t, 3> {0x00, 0xFF, 0x00}));
}

/**
 * @brief The X11 backend refuses an unsupported visual layout instead of
 *        capturing with an ambiguous pixel format.
 */
TEST(X11IntegrationTests, RejectsUnsupportedVisualDepth) {
  xvfb::xvfb_server_t server;
  const auto server_result = server.start(640, 480, 16);
  if (server_result == xvfb::start_result_e::unavailable) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "Xvfb is not available";
    GTEST_SKIP() << "Xvfb is not available";
  }
  ASSERT_EQ(server_result, xvfb::start_result_e::started) << "Xvfb failed to start";

  // The server itself must be reachable; the rejection has to come from the
  // backend's visual validation, not from a missing display.
  if (x11_test::xdisplay_ptr xdisplay(XOpenDisplay(nullptr), &XCloseDisplay); !xdisplay) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "Could not open the Xvfb display";
    GTEST_SKIP() << "Could not open the Xvfb display";
  }

  video::config_t config {};
  config.width = 640;
  config.height = 480;
  config.framerate = 60;

  // A 16-bit TrueColor visual has no unambiguous declared layout, so the
  // backend must refuse it instead of reading the buffer as packed BGR0.
  EXPECT_EQ(platf::x11_display(platf::mem_type_e::system, "", config), nullptr);
}

#endif
