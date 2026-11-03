/**
 * @file tests/unit/platform/linux/test_cuda.cpp
 * @brief Tests for Linux CUDA capture selection.
 */

#ifdef __linux__

  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <array>
  #include <cstdint>
  #include <string>
  #include <vector>

  // lib includes
  #include <glad/gl.h>

  // local includes
  #include "src/platform/linux/cuda.h"

namespace {
  /**
   * @brief Expected virtual desktop selection for an NvFBC status.
   */
  struct nvfbc_virtual_desktop_case_t {
    std::uint32_t output_count;  ///< Physical outputs reported by NvFBC.
    bool capture_possible;  ///< Whether NvFBC reports capture support.
    std::uint32_t screen_width;  ///< Virtual desktop width.
    std::uint32_t screen_height;  ///< Virtual desktop height.
    bool expected;  ///< Whether Sunshine should offer the virtual desktop.
  };
}  // namespace

TEST(NvFbcDisplaySelectionTest, OffersVirtualDesktopOnlyForValidNoOutputCapture) {
  constexpr std::array cases {
    nvfbc_virtual_desktop_case_t {1U, true, 1920U, 1080U, false},
    nvfbc_virtual_desktop_case_t {0U, true, 1920U, 1080U, true},
    nvfbc_virtual_desktop_case_t {0U, false, 1920U, 1080U, false},
    nvfbc_virtual_desktop_case_t {0U, true, 0U, 1080U, false},
    nvfbc_virtual_desktop_case_t {0U, true, 1920U, 0U, false},
  };

  for (const auto &[output_count, capture_possible, screen_width, screen_height, expected] : cases) {
    EXPECT_EQ(cuda::nvfbc::should_offer_virtual_desktop(output_count, capture_possible, screen_width, screen_height), expected)
      << "output_count=" << output_count
      << ", capture_possible=" << capture_possible
      << ", screen_width=" << screen_width
      << ", screen_height=" << screen_height;
  }
}

  #ifdef SUNSHINE_BUILD_CUDA
namespace {
  /**
   * @brief Fake GL->CUDA handoff steps that record their order and check plane access.
   *
   * GL must not render into the planes while they are mapped into CUDA or before
   * CUDA has copied the previous frame out, and CUDA must not copy them before GL
   * has finished rendering.
   */
  struct fake_handoff_t {
    std::vector<std::string> calls;  ///< Steps in the order they ran.
    std::string failing_step;  ///< Step that returns an error, or empty.
    int failing_plane = -1;  ///< Plane whose copy returns an error, or -1.
    bool mapped = false;  ///< Whether the planes are mapped into CUDA.
    bool gl_finished = false;  ///< Whether GL has finished the current frame's rendering.
    bool copies_finished = true;  ///< Whether CUDA has finished the previous frame's copies.

    /**
     * @brief Record a step and return its result.
     *
     * @param name Step name.
     * @return -1 when `name` is the failing step, 0 otherwise.
     */
    int step(const std::string &name) {
      calls.push_back(name);
      return name == failing_step ? -1 : 0;
    }

    /**
     * @brief Handoff steps backed by this fake.
     *
     * @return The steps.
     */
    cuda::gl_cuda_handoff_ops_t ops() {
      return {
        .wait_for_cuda = [this] {
          const int status = step("wait_for_cuda");
          copies_finished = copies_finished || status == 0;
          return status;
        },
        .render = [this] {
          calls.emplace_back("render");
          EXPECT_FALSE(mapped) << "GL rendered into planes mapped into CUDA";
          EXPECT_TRUE(copies_finished) << "GL rendered before CUDA copied the previous frame";
          gl_finished = false;
        },
        .wait_for_gl = [this] {
          const int status = step("wait_for_gl");
          gl_finished = status == 0;
          return status;
        },
        .map = [this] {
          const int status = step("map");
          mapped = status == 0;
          gl_finished = mapped;  // Mapping waits for GL.
          return status;
        },
        .copy = [this](int plane) {
          calls.push_back("copy" + std::to_string(plane));
          EXPECT_TRUE(gl_finished) << "CUDA copied before GL finished rendering";
          copies_finished = false;
          return plane == failing_plane ? -1 : 0;
        },
        .unmap = [this] {
          EXPECT_TRUE(mapped) << "unmapped planes that weren't mapped";
          mapped = false;
          copies_finished = true;  // Unmapping orders the copies before later GL work.
          return step("unmap");
        },
        .record_copies = [this] {
          return step("record_copies");
        },
      };
    }
  };

  /**
   * @brief Expected steps of one successful frame.
   *
   * @param shared_planes Whether the planes are shared through external memory.
   * @param plane_count Number of planes.
   * @return The steps in order.
   */
  std::vector<std::string> successful_frame(bool shared_planes, int plane_count) {
    std::vector<std::string> steps;
    if (shared_planes) {
      steps.emplace_back("wait_for_cuda");
    }
    steps.emplace_back("render");
    steps.emplace_back(shared_planes ? "wait_for_gl" : "map");
    for (int plane = 0; plane < plane_count; ++plane) {
      steps.push_back("copy" + std::to_string(plane));
    }
    steps.emplace_back(shared_planes ? "record_copies" : "unmap");
    return steps;
  }

  /**
   * @brief Plane counts: two for NV12 and P010, three for YUV 4:4:4.
   */
  class GlCudaHandoffPlanesTest: public testing::TestWithParam<int> {};
}  // namespace

TEST_P(GlCudaHandoffPlanesTest, SharedPlanesTakeTurnsAcrossFrames) {
  const int plane_count = GetParam();
  fake_handoff_t fake;

  ASSERT_EQ(cuda::gl_cuda_handoff(true, plane_count, fake.ops()), 0);
  ASSERT_EQ(cuda::gl_cuda_handoff(true, plane_count, fake.ops()), 0);

  auto expected = successful_frame(true, plane_count);
  const auto second_frame = expected;
  expected.insert(expected.end(), second_frame.begin(), second_frame.end());
  EXPECT_EQ(fake.calls, expected);
}

TEST_P(GlCudaHandoffPlanesTest, MappedPlanesAreMappedOnlyForTheCopies) {
  const int plane_count = GetParam();
  fake_handoff_t fake;

  ASSERT_EQ(cuda::gl_cuda_handoff(false, plane_count, fake.ops()), 0);
  ASSERT_EQ(cuda::gl_cuda_handoff(false, plane_count, fake.ops()), 0);

  auto expected = successful_frame(false, plane_count);
  const auto second_frame = expected;
  expected.insert(expected.end(), second_frame.begin(), second_frame.end());
  EXPECT_EQ(fake.calls, expected);
  EXPECT_FALSE(fake.mapped);
}

INSTANTIATE_TEST_SUITE_P(PlaneCounts, GlCudaHandoffPlanesTest, testing::Values(2, 3));

TEST(GlCudaHandoffTest, SharedPlanesStopWhenWaitingForCudaFails) {
  fake_handoff_t fake {.failing_step = "wait_for_cuda"};

  EXPECT_EQ(cuda::gl_cuda_handoff(true, 2, fake.ops()), -1);
  EXPECT_EQ(fake.calls, (std::vector<std::string> {"wait_for_cuda"}));
}

TEST(GlCudaHandoffTest, SharedPlanesSkipTheCopiesWhenWaitingForGlFails) {
  fake_handoff_t fake {.failing_step = "wait_for_gl"};

  EXPECT_EQ(cuda::gl_cuda_handoff(true, 2, fake.ops()), -1);
  EXPECT_EQ(fake.calls, (std::vector<std::string> {"wait_for_cuda", "render", "wait_for_gl"}));
}

TEST(GlCudaHandoffTest, SharedPlanesRecordQueuedCopiesWhenACopyFails) {
  fake_handoff_t fake {.failing_plane = 0};

  EXPECT_EQ(cuda::gl_cuda_handoff(true, 3, fake.ops()), -1);
  EXPECT_EQ(fake.calls, (std::vector<std::string> {"wait_for_cuda", "render", "wait_for_gl", "copy0", "record_copies"}));
}

TEST(GlCudaHandoffTest, SharedPlanesFailWhenRecordingTheCopiesFails) {
  fake_handoff_t fake {.failing_step = "record_copies"};

  EXPECT_EQ(cuda::gl_cuda_handoff(true, 2, fake.ops()), -1);
  EXPECT_EQ(fake.calls, successful_frame(true, 2));
}

TEST(GlCudaHandoffTest, MappedPlanesSkipTheCopiesWhenMappingFails) {
  fake_handoff_t fake {.failing_step = "map"};

  EXPECT_EQ(cuda::gl_cuda_handoff(false, 2, fake.ops()), -1);
  EXPECT_EQ(fake.calls, (std::vector<std::string> {"render", "map"}));
  EXPECT_FALSE(fake.mapped);
}

TEST(GlCudaHandoffTest, MappedPlanesAreUnmappedOnceWhenACopyFails) {
  fake_handoff_t fake {.failing_plane = 1};

  EXPECT_EQ(cuda::gl_cuda_handoff(false, 3, fake.ops()), -1);
  EXPECT_EQ(fake.calls, (std::vector<std::string> {"render", "map", "copy0", "copy1", "unmap"}));
  EXPECT_FALSE(fake.mapped);

  // The next frame starts with the planes back in GL's hands.
  fake.failing_plane = -1;
  EXPECT_EQ(cuda::gl_cuda_handoff(false, 3, fake.ops()), 0);
}

TEST(GlCudaHandoffTest, MappedPlanesFailWhenUnmappingFails) {
  fake_handoff_t fake {.failing_step = "unmap"};

  EXPECT_EQ(cuda::gl_cuda_handoff(false, 2, fake.ops()), -1);
  EXPECT_EQ(fake.calls, successful_frame(false, 2));
}

namespace {
  /**
   * @brief Fake GL fence that records how it was used.
   */
  struct fake_fence_t {
    bool create_fails = false;  ///< Whether creating the fence fails.
    std::uint32_t wait_status = GL_ALREADY_SIGNALED;  ///< Status the wait returns.
    int waits = 0;  ///< Number of waits.
    int destroys = 0;  ///< Number of deletions.

    /**
     * @brief Fence calls backed by this fake.
     *
     * @return The calls.
     */
    cuda::gl_fence_ops_t ops() {
      return {
        .create = [this] {
          return !create_fails;
        },
        .client_wait = [this] {
          ++waits;
          return wait_status;
        },
        .destroy = [this] {
          ++destroys;
        },
      };
    }
  };

  /**
   * @brief GL wait statuses and whether waiting for GL succeeds with them.
   */
  class GlFenceWaitStatusTest: public testing::TestWithParam<std::pair<std::uint32_t, int>> {};
}  // namespace

TEST(GlFenceWaitTest, FailsWithoutWaitingWhenTheFenceCannotBeCreated) {
  fake_fence_t fake {.create_fails = true};

  EXPECT_EQ(cuda::wait_for_gl_fence(fake.ops()), -1);
  EXPECT_EQ(fake.waits, 0);
  EXPECT_EQ(fake.destroys, 0);
}

TEST_P(GlFenceWaitStatusTest, DeletesTheFenceOnceAndReportsTheStatus) {
  const auto [status, expected] = GetParam();
  fake_fence_t fake {.wait_status = status};

  EXPECT_EQ(cuda::wait_for_gl_fence(fake.ops()), expected);
  EXPECT_EQ(fake.waits, 1);
  EXPECT_EQ(fake.destroys, 1);
}

INSTANTIATE_TEST_SUITE_P(
  Statuses,
  GlFenceWaitStatusTest,
  testing::Values(
    std::pair<std::uint32_t, int> {GL_ALREADY_SIGNALED, 0},
    std::pair<std::uint32_t, int> {GL_CONDITION_SATISFIED, 0},
    std::pair<std::uint32_t, int> {GL_TIMEOUT_EXPIRED, -1},
    std::pair<std::uint32_t, int> {GL_WAIT_FAILED, -1}
  )
);
  #endif  // SUNSHINE_BUILD_CUDA

#endif  // __linux__
