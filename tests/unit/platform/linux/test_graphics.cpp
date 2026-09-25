/**
 * @file tests/unit/platform/linux/test_graphics.cpp
 * @brief Test src/platform/linux/graphics.h image descriptor behavior.
 */
#include "../../../tests_common.h"

#if defined(__linux__)
  #include <algorithm>
  #include <exception>

  #include <src/platform/linux/graphics.h>

namespace {
  /**
   * @brief Test-only failure raised by a capture-buffer release callback.
   */
  class capture_buffer_release_error: public std::exception {};
}  // namespace

TEST(EglImageDescriptorTest, ReleasesCaptureBufferOnlyOnce) {
  egl::img_descriptor_t descriptor;
  std::ranges::fill(descriptor.sd.fds, -1);

  int release_count = 0;
  descriptor.capture_buffer_consumed_cb = [&release_count]() {
    ++release_count;
  };

  descriptor.mark_capture_buffer_consumed();
  descriptor.mark_capture_buffer_consumed();
  descriptor.reset();

  EXPECT_EQ(release_count, 1);
}

TEST(EglImageDescriptorTest, ResetReleasesCaptureBuffer) {
  egl::img_descriptor_t descriptor;
  std::ranges::fill(descriptor.sd.fds, -1);

  bool released = false;
  descriptor.capture_buffer_consumed_cb = [&released]() {
    released = true;
  };

  descriptor.reset();

  EXPECT_TRUE(released);
}

TEST(EglImageDescriptorTest, ContainsCaptureBufferReleaseExceptions) {
  egl::img_descriptor_t descriptor;
  std::ranges::fill(descriptor.sd.fds, -1);

  descriptor.capture_buffer_consumed_cb = []() {
    throw capture_buffer_release_error {};
  };

  EXPECT_NO_THROW(descriptor.mark_capture_buffer_consumed());
  EXPECT_FALSE(descriptor.capture_buffer_consumed_cb);
}
#endif
