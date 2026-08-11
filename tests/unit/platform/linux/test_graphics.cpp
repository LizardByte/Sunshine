/**
 * @file tests/unit/platform/linux/test_graphics.cpp
 * @brief Test src/platform/linux/graphics.h image descriptor behavior.
 */
#include "../../../tests_common.h"

#if defined(__linux__)
  #include <algorithm>
  #include <iterator>

  #include <src/platform/linux/graphics.h>

TEST(EglImageDescriptorTest, ReleasesCaptureBufferOnlyOnce) {
  egl::img_descriptor_t descriptor;
  std::fill(std::begin(descriptor.sd.fds), std::end(descriptor.sd.fds), -1);

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
  std::fill(std::begin(descriptor.sd.fds), std::end(descriptor.sd.fds), -1);

  bool released = false;
  descriptor.capture_buffer_consumed_cb = [&released]() {
    released = true;
  };

  descriptor.reset();

  EXPECT_TRUE(released);
}
#endif
