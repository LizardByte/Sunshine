/**
 * @file tests/integration/test_egl_ram_upload.cpp
 * @brief Integration tests for the EGL RAM upload path against a real GL context.
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_X11)

  // test includes
  #include "../tests_common.h"
  #include "xvfb_server.h"

  // standard includes
  #include <cstdint>
  #include <vector>

  // local includes
  #include <src/platform/linux/graphics.h>
  #include <src/platform/linux/misc.h>
  #include <src/platform/linux/x11grab.h>
  #include <src/utility.h>

extern window_system_e window_system;  // NOSONAR(cpp:S5421) - defined by src/platform/linux/misc.cpp; the test swaps it temporarily

namespace platf {
  int load_x11();
}  // namespace platf

/**
 * @brief `load_ram` uploads padded frames, restores the surrounding unpack
 *        state, and fails closed for layouts it cannot represent.
 */
TEST(EglUploadIntegrationTests, LoadRamUploadsAndRestoresUnpackState) {
  // The software GL driver of the ARM64 Alpine image is broken (the upload
  // segfaulted on one run and hung for hours on another), so skip there
  // instead of taking the whole suite down.
  if (xvfb::gl_integration_unreliable()) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "GL integration is not reliable on this platform";
    GTEST_SKIP() << "GL integration is not reliable on this platform";
  }

  xvfb::xvfb_server_t server;
  const auto server_result = server.start();
  if (server_result == xvfb::start_result_e::unavailable) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "Xvfb is not available";
    GTEST_SKIP() << "Xvfb is not available";
  }
  ASSERT_EQ(server_result, xvfb::start_result_e::started) << "Xvfb failed to start";

  // The test serves its own X server; make load_x11() accept the session even
  // when the surrounding environment is a Wayland desktop.
  const auto previous_window_system = window_system;
  window_system = window_system_e::X11;
  const auto restore_window_system = util::fail_guard([previous_window_system] {
    window_system = previous_window_system;
  });
  const int x11_status = platf::load_x11();
  if (x11_status) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "X11 entry points are not available";
    GTEST_SKIP() << "X11 entry points are not available";
  }

  if (!gladLoaderLoadEGL(nullptr)) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "EGL entry points are not available";
    GTEST_SKIP() << "EGL entry points are not available";
  }

  auto xdisplay = platf::x11::make_display();
  if (!xdisplay) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "X11 display is not available";
    GTEST_SKIP() << "X11 display is not available";
  }

  auto egl_display = egl::make_display(xdisplay.get());
  if (!egl_display) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "EGL display is not available";
    GTEST_SKIP() << "EGL display is not available";
  }

  auto ctx = egl::make_ctx(egl_display.get());
  if (!ctx) {
    ASSERT_FALSE(xvfb::integration_environment_required()) << "EGL context is not available";
    GTEST_SKIP() << "EGL context is not available";
  }

  constexpr int width = 320;
  constexpr int height = 240;
  constexpr int padded_row_pitch = width * 4 + 64;

  auto sws = egl::sws_t::make(width, height, width, height, AV_PIX_FMT_YUV420P, false);
  ASSERT_TRUE(sws.has_value());

  std::vector<std::uint8_t> buffer(static_cast<std::size_t>(padded_row_pitch) * height, 0x40);

  // A row-dependent pattern makes a wrong row length visible in the texture.
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t offset = static_cast<std::size_t>(y) * padded_row_pitch + static_cast<std::size_t>(x) * 4;
      buffer[offset + 0] = static_cast<std::uint8_t>(y);
      buffer[offset + 1] = static_cast<std::uint8_t>(y);
      buffer[offset + 2] = static_cast<std::uint8_t>(y);
      buffer[offset + 3] = 0xFF;
    }
  }

  platf::img_t img {};
  img.data = buffer.data();
  img.width = width;
  img.height = height;
  img.pixel_pitch = 4;
  img.pixel_format = platf::pix_fmt_e::bgr0;

  // The caller may have set an unpack row length; the upload must restore it.
  // The test restores the surrounding GL state on every exit path so a failed
  // assertion cannot leak it into the later tests sharing this process.
  GLint previous_unpack_row_length = 0;
  gl::ctx.GetIntegerv(GL_UNPACK_ROW_LENGTH, &previous_unpack_row_length);
  gl::ctx.PixelStorei(GL_UNPACK_ROW_LENGTH, 7);
  const auto restore_gl_state = util::fail_guard([previous_unpack_row_length] {
    gl::ctx.BindTexture(GL_TEXTURE_2D, 0);
    gl::ctx.PixelStorei(GL_UNPACK_ROW_LENGTH, previous_unpack_row_length);
  });

  GLint observed = 0;
  const auto expect_state_restored = [&observed]() {
    gl::ctx.GetIntegerv(GL_UNPACK_ROW_LENGTH, &observed);
    EXPECT_EQ(observed, 7) << "load_ram must restore the surrounding unpack state";
  };

  img.row_pitch = padded_row_pitch;
  EXPECT_EQ(sws->load_ram(img), 0) << "a padded but complete stride must upload";
  expect_state_restored();

  // The padded stride must have been honored: if the padding were ignored, the
  // second texture row would hold the first row's pixels.
  gl::ctx.BindTexture(GL_TEXTURE_2D, sws->tex[0]);
  std::vector<std::uint8_t> readback(static_cast<std::size_t>(width) * height * 4);
  gl::ctx.GetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, readback.data());
  EXPECT_EQ(gl::ctx.GetError(), GL_NO_ERROR);
  EXPECT_EQ(readback[0], 0x00) << "first texture row";
  EXPECT_EQ(readback[static_cast<std::size_t>(width) * 4], 0x01) << "second texture row";
  EXPECT_EQ(readback[static_cast<std::size_t>(height - 1) * width * 4], static_cast<std::uint8_t>(height - 1)) << "last texture row";

  // A stride that cannot describe a complete packed row fails closed without
  // touching the GL state.
  img.row_pitch = width * 4 - 4;
  EXPECT_EQ(sws->load_ram(img), -1);
  expect_state_restored();

  // Declared formats this path cannot represent fail closed.
  img.row_pitch = width * 4;
  img.pixel_pitch = 1;
  img.pixel_format = platf::pix_fmt_e::nv12;
  EXPECT_EQ(sws->load_ram(img), -1);
  expect_state_restored();
}

#endif
