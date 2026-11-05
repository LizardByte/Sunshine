/**
 * @file tests/unit/platform/linux/test_cuda.cpp
 * @brief Tests for Linux CUDA capture selection.
 */

#ifdef __linux__

  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <algorithm>
  #include <array>
  #include <cstdint>
  #include <map>
  #include <memory>
  #include <ranges>
  #include <set>
  #include <string>
  #include <utility>
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

namespace {
  /**
   * @brief Fake shared memory driver that records its calls and which descriptors it was given.
   */
  class fake_shared_memory_t final: public cuda::shared_memory_driver_t {
  public:
    /**
     * @brief Format and size a plane step was given.
     */
    struct plane_args_t {
      std::uint32_t format;  ///< Sized GL internal format.
      int width;  ///< Plane width.
      int height;  ///< Plane height.

      /**
       * @brief Compare all fields.
       */
      bool operator==(const plane_args_t &) const = default;
    };

    /**
     * @brief What a test wants from and sees in the fake, kept alive after the planes own the driver.
     */
    struct state_t {
      bool is_supported = true;  ///< What supported() returns.
      std::string failing_step;  ///< Step that fails, or empty when none does.
      std::size_t failing_plane = 0;  ///< Plane that `failing_step` fails on.
      int failing_export = 1;  ///< Which of a plane's exports fails, counting from 1.
      std::vector<std::string> calls;  ///< Every call, as "step" or "step:plane".
      std::set<int> open_fds;  ///< Exported descriptors that nobody has taken over or closed.
      std::map<int, std::size_t> imported_fds;  ///< Imported descriptors and the number of imports of each.
      std::vector<CUDA_EXTERNAL_MEMORY_HANDLE_DESC> memory_descs;  ///< Descriptors given to import_cuda().
      std::vector<CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC> array_descs;  ///< Descriptors given to map_array().
      std::vector<plane_args_t> memory_args;  ///< Arguments given to allocate_memory().
      std::vector<plane_args_t> storage_args;  ///< Arguments given to create_gl_storage().
      std::vector<std::uint64_t> gl_sizes;  ///< Sizes given to import_gl().
      int next_fd = 100;  ///< Next descriptor to export.

      /**
       * @brief Count the calls of one step.
       *
       * @param call Call as recorded in `calls`.
       * @return The number of those calls.
       */
      auto count(const std::string &call) const {
        return std::ranges::count(calls, call);
      }

      /**
       * @brief Find the position of a call.
       *
       * @param call Call as recorded in `calls`.
       * @return Position of its first occurrence, or the number of calls when it wasn't made.
       */
      auto position(const std::string &call) const {
        return std::ranges::find(calls, call) - calls.begin();
      }
    };

    /**
     * @brief Construct the fake.
     *
     * @param test_state State shared with the test.
     */
    explicit fake_shared_memory_t(std::shared_ptr<state_t> test_state):
        state {std::move(test_state)} {
    }

    bool supported() override {  ///< Configured answer.
      state->calls.emplace_back("supported");
      return state->is_supported;
    }

    int create_device() override {  ///< Recorded step.
      return step("create_device");
    }

    void destroy_device() override {  ///< Recorded step.
      state->calls.emplace_back("destroy_device");
    }

    int create_event() override {  ///< Recorded step.
      return step("create_event");
    }

    void destroy_event() override {  ///< Recorded step.
      state->calls.emplace_back("destroy_event");
    }

    int wait_event() override {  ///< Recorded step.
      return step("wait_event");
    }

    int record_event(CUstream) override {  ///< Recorded step.
      return step("record_event");
    }

    int allocate_memory(std::size_t plane, std::uint32_t internal_format, int width, int height, std::uint64_t &size) override {  ///< Recorded step, with a size derived from the plane.
      state->memory_args.push_back({internal_format, width, height});
      size = 4096 * (plane + 1);
      return step("allocate_memory", plane);
    }

    void free_memory(std::size_t plane) override {  ///< Recorded step.
      record("free_memory", plane);
    }

    int export_fd(std::size_t plane, int &fd) override {  ///< Recorded step, opening a new descriptor.
      record("export_fd", plane);
      if (state->failing_step == "export_fd" && state->failing_plane == plane && state->failing_export == ++exports[plane]) {
        return -1;
      }

      fd = state->next_fd++;
      state->open_fds.insert(fd);
      return 0;
    }

    void close_fd(int fd) override {  ///< Recorded step. The descriptor must be open.
      state->calls.emplace_back("close_fd");
      EXPECT_EQ(state->open_fds.erase(fd), 1u) << "closed a descriptor that wasn't open: " << fd;
    }

    int import_gl(std::size_t plane, int fd, std::uint64_t size) override {  ///< Recorded step, taking over the descriptor on success.
      state->gl_sizes.push_back(size);
      if (step("import_gl", plane)) {
        return -1;
      }

      take_over(fd);
      return 0;
    }

    int create_gl_storage(std::size_t plane, std::uint32_t internal_format, int width, int height) override {  ///< Recorded step.
      state->storage_args.push_back({internal_format, width, height});
      return step("create_gl_storage", plane);
    }

    void release_gl(std::size_t plane) override {  ///< Recorded step.
      record("release_gl", plane);
    }

    int import_cuda(std::size_t plane, const CUDA_EXTERNAL_MEMORY_HANDLE_DESC &desc) override {  ///< Recorded step, taking over the descriptor on success.
      state->memory_descs.push_back(desc);
      if (step("import_cuda", plane)) {
        return -1;
      }

      take_over(desc.handle.fd);
      return 0;
    }

    void release_cuda_memory(std::size_t plane) override {  ///< Recorded step.
      record("release_cuda_memory", plane);
    }

    int map_array(std::size_t plane, const CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC &desc) override {  ///< Recorded step.
      state->array_descs.push_back(desc);
      return step("map_array", plane);
    }

    void release_array(std::size_t plane) override {  ///< Recorded step.
      record("release_array", plane);
    }

    CUarray array(std::size_t plane) const override {  ///< Recorded step. The fake has no arrays.
      state->calls.push_back("array:" + std::to_string(plane));
      return nullptr;
    }

  private:
    void record(const std::string &name, std::size_t plane) {  ///< Record a call on a plane.
      state->calls.push_back(name + ":" + std::to_string(plane));
    }

    int step(const std::string &name) {  ///< Record a device step, returning -1 when configured to fail.
      state->calls.push_back(name);
      return state->failing_step == name ? -1 : 0;
    }

    int step(const std::string &name, std::size_t plane) {  ///< Record a plane step, returning -1 when configured to fail.
      record(name, plane);
      return state->failing_step == name && state->failing_plane == plane ? -1 : 0;
    }

    void take_over(int fd) {  ///< Move an open descriptor to an importing API.
      EXPECT_EQ(state->open_fds.erase(fd), 1u) << "imported a descriptor that wasn't open: " << fd;
      ++state->imported_fds[fd];
    }

    std::shared_ptr<state_t> state;  ///< State shared with the test.
    std::array<int, 3> exports {};  ///< Number of exports of each plane.
  };

  /**
   * @brief Fake state and planes built on it.
   */
  struct shared_planes_fixture_t {
    std::shared_ptr<fake_shared_memory_t::state_t> state = std::make_shared<fake_shared_memory_t::state_t>();  ///< State of the fake.
    cuda::gl_cuda_shared_planes_t planes {std::make_unique<fake_shared_memory_t>(state)};  ///< Planes under test.
  };

  /**
   * @brief Expected layout of a plane format.
   */
  struct plane_format_case_t {
    std::uint32_t gl_format;  ///< Sized GL internal format.
    CUarray_format cu_format;  ///< Expected CUDA array format.
    unsigned int channels;  ///< Expected channels per pixel.
    #ifdef SUNSHINE_BUILD_VULKAN
    VkFormat vk_format;  ///< Expected Vulkan format.
    #endif
  };

  /**
   * @brief All plane formats a stream can use.
   */
  constexpr std::array plane_format_cases {
    #ifdef SUNSHINE_BUILD_VULKAN
    plane_format_case_t {GL_R8, CU_AD_FORMAT_UNSIGNED_INT8, 1, VK_FORMAT_R8_UNORM},
    plane_format_case_t {GL_RG8, CU_AD_FORMAT_UNSIGNED_INT8, 2, VK_FORMAT_R8G8_UNORM},
    plane_format_case_t {GL_R16, CU_AD_FORMAT_UNSIGNED_INT16, 1, VK_FORMAT_R16_UNORM},
    plane_format_case_t {GL_RG16, CU_AD_FORMAT_UNSIGNED_INT16, 2, VK_FORMAT_R16G16_UNORM},
    #else
    plane_format_case_t {GL_R8, CU_AD_FORMAT_UNSIGNED_INT8, 1},
    plane_format_case_t {GL_RG8, CU_AD_FORMAT_UNSIGNED_INT8, 2},
    plane_format_case_t {GL_R16, CU_AD_FORMAT_UNSIGNED_INT16, 1},
    plane_format_case_t {GL_RG16, CU_AD_FORMAT_UNSIGNED_INT16, 2},
    #endif
  };

  /**
   * @brief Tests that run for every plane format.
   */
  class CudaGlInteropFormatTest: public testing::TestWithParam<plane_format_case_t> {};

  /**
   * @brief Allocation steps that can fail on the second plane.
   */
  class CudaGlInteropFailureTest: public testing::TestWithParam<std::string> {};

  /**
   * @brief All planes' steps that must be released once when they were started.
   */
  constexpr std::array release_steps {"release_array", "release_cuda_memory", "release_gl", "free_memory"};
}  // namespace

TEST(CudaGlInteropTest, InitFailsWithoutADriver) {
  cuda::gl_cuda_shared_planes_t planes {nullptr};

  EXPECT_EQ(planes.init(), -1);
  EXPECT_FALSE(planes.is_active());
  planes.reset();
}

TEST(CudaGlInteropTest, InitFailsWithoutTouchingTheDeviceWhenUnsupported) {
  shared_planes_fixture_t f;
  f.state->is_supported = false;

  EXPECT_EQ(f.planes.init(), -1);
  EXPECT_FALSE(f.planes.is_active());
  f.planes.reset();
  EXPECT_EQ(f.state->calls, std::vector<std::string> {"supported"});
}

TEST(CudaGlInteropTest, DeviceFailureDestroysTheDeviceOnceAndSkipsTheEvent) {
  shared_planes_fixture_t f;
  f.state->failing_step = "create_device";

  EXPECT_EQ(f.planes.init(), -1);
  EXPECT_FALSE(f.planes.is_active());
  f.planes.reset();
  f.planes.reset();
  EXPECT_EQ(f.state->calls, (std::vector<std::string> {"supported", "create_device", "destroy_device"}));
}

TEST(CudaGlInteropTest, EventFailureReleasesTheEventBeforeTheDevice) {
  shared_planes_fixture_t f;
  f.state->failing_step = "create_event";

  EXPECT_EQ(f.planes.init(), -1);
  EXPECT_FALSE(f.planes.is_active());
  f.planes.reset();
  f.planes.reset();
  EXPECT_EQ(f.state->calls, (std::vector<std::string> {"supported", "create_device", "create_event", "destroy_event", "destroy_device"}));
}

TEST(CudaGlInteropTest, UnsupportedFormatTouchesNothing) {
  shared_planes_fixture_t f;

  EXPECT_EQ(f.planes.init(), 0);
  f.state->calls.clear();
  EXPECT_EQ(f.planes.allocate(0, GL_RGBA8, 64, 32), -1);
  EXPECT_TRUE(f.state->calls.empty());
}

TEST(CudaGlInteropTest, EverythingIsReleasedOnceInOrderAndResetIsRepeatable) {
  shared_planes_fixture_t f;

  ASSERT_EQ(f.planes.init(), 0);
  EXPECT_TRUE(f.planes.is_active());
  for (std::size_t plane = 0; plane < 3; ++plane) {
    ASSERT_EQ(f.planes.allocate(plane, GL_R8, 64, 32), 0);
  }
  EXPECT_TRUE(f.state->open_fds.empty());

  f.state->calls.clear();
  f.planes.reset();
  EXPECT_FALSE(f.planes.is_active());
  const std::vector<std::string> expected {
    "release_array:0",
    "release_cuda_memory:0",
    "release_gl:0",
    "free_memory:0",
    "release_array:1",
    "release_cuda_memory:1",
    "release_gl:1",
    "free_memory:1",
    "release_array:2",
    "release_cuda_memory:2",
    "release_gl:2",
    "free_memory:2",
    "destroy_event",
    "destroy_device",
  };
  EXPECT_EQ(f.state->calls, expected);

  f.planes.reset();
  EXPECT_EQ(f.state->calls, expected);
}

TEST(CudaGlInteropTest, ArrayComesFromTheRequestedPlane) {
  shared_planes_fixture_t f;

  EXPECT_EQ(f.planes.array(2), nullptr);
  EXPECT_EQ(f.state->count("array:2"), 1);
}

TEST(CudaGlInteropTest, DestructionReleasesEverything) {
  const auto state = std::make_shared<fake_shared_memory_t::state_t>();
  {
    cuda::gl_cuda_shared_planes_t planes {std::make_unique<fake_shared_memory_t>(state)};
    ASSERT_EQ(planes.init(), 0);
    ASSERT_EQ(planes.allocate(0, GL_R8, 64, 32), 0);
  }

  EXPECT_EQ(state->count("free_memory:0"), 1);
  EXPECT_EQ(state->count("destroy_device"), 1);
}

TEST(CudaGlInteropTest, WaitsForCudaOnlyAfterCopiesWereRecorded) {
  shared_planes_fixture_t f;
  ASSERT_EQ(f.planes.init(), 0);

  EXPECT_EQ(f.planes.wait_for_cuda(), 0);
  EXPECT_EQ(f.state->count("wait_event"), 0);

  EXPECT_EQ(f.planes.record_copies(nullptr), 0);
  EXPECT_EQ(f.planes.wait_for_cuda(), 0);
  EXPECT_EQ(f.state->count("wait_event"), 1);

  // That wait consumed the recorded copies.
  EXPECT_EQ(f.planes.wait_for_cuda(), 0);
  EXPECT_EQ(f.state->count("wait_event"), 1);
}

TEST(CudaGlInteropTest, FailedWaitIsReportedAndNotRepeated) {
  shared_planes_fixture_t f;
  ASSERT_EQ(f.planes.init(), 0);
  ASSERT_EQ(f.planes.record_copies(nullptr), 0);
  f.state->failing_step = "wait_event";

  EXPECT_EQ(f.planes.wait_for_cuda(), -1);
  EXPECT_EQ(f.planes.wait_for_cuda(), 0);
  EXPECT_EQ(f.state->count("wait_event"), 1);
}

TEST(CudaGlInteropTest, FailedRecordLeavesNothingToWaitFor) {
  shared_planes_fixture_t f;
  ASSERT_EQ(f.planes.init(), 0);
  f.state->failing_step = "record_event";

  EXPECT_EQ(f.planes.record_copies(nullptr), -1);
  EXPECT_EQ(f.planes.wait_for_cuda(), 0);
  EXPECT_EQ(f.state->count("wait_event"), 0);
}

TEST(CudaGlInteropTest, ResetForgetsRecordedCopies) {
  shared_planes_fixture_t f;
  ASSERT_EQ(f.planes.init(), 0);
  ASSERT_EQ(f.planes.record_copies(nullptr), 0);

  f.planes.reset();

  EXPECT_EQ(f.planes.wait_for_cuda(), 0);
  EXPECT_EQ(f.state->count("wait_event"), 0);
}

TEST_P(CudaGlInteropFormatTest, FormatMapsToItsLayout) {
  const auto &expected = GetParam();

  const auto format = cuda::shared_plane_format(expected.gl_format);

  ASSERT_TRUE(format.has_value());
  EXPECT_EQ(format->gl_format, expected.gl_format);
  EXPECT_EQ(format->cu_format, expected.cu_format);
  EXPECT_EQ(format->channels, expected.channels);
}

TEST_P(CudaGlInteropFormatTest, ImportsDescribeThePlane) {
  const auto &expected = GetParam();
  shared_planes_fixture_t f;
  ASSERT_EQ(f.planes.init(), 0);

  ASSERT_EQ(f.planes.allocate(1, expected.gl_format, 640, 360), 0);

  EXPECT_EQ(f.state->gl_sizes, std::vector<std::uint64_t> {4096 * 2});
  const std::vector<fake_shared_memory_t::plane_args_t> plane_args {{expected.gl_format, 640, 360}};
  EXPECT_EQ(f.state->memory_args, plane_args);
  EXPECT_EQ(f.state->storage_args, plane_args);

  ASSERT_EQ(f.state->memory_descs.size(), 1u);
  const auto &memory = f.state->memory_descs[0];
  EXPECT_EQ(memory.type, CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD);
  EXPECT_EQ(memory.size, 4096u * 2);
  EXPECT_EQ(memory.flags, 0x1u);  // CUDA_EXTERNAL_MEMORY_DEDICATED

  ASSERT_EQ(f.state->array_descs.size(), 1u);
  const auto &array = f.state->array_descs[0];
  EXPECT_EQ(array.offset, 0u);
  EXPECT_EQ(array.numLevels, 1u);
  EXPECT_EQ(array.arrayDesc.Width, 640u);
  EXPECT_EQ(array.arrayDesc.Height, 360u);
  EXPECT_EQ(array.arrayDesc.Depth, 0u);
  EXPECT_EQ(array.arrayDesc.Format, expected.cu_format);
  EXPECT_EQ(array.arrayDesc.NumChannels, expected.channels);
  EXPECT_EQ(array.arrayDesc.Flags, 0x20u);  // CUDA_ARRAY3D_COLOR_ATTACHMENT

  // GL and CUDA each got their own descriptor.
  EXPECT_TRUE(f.state->open_fds.empty());
  EXPECT_EQ(f.state->imported_fds.size(), 2u);
}

    #ifdef SUNSHINE_BUILD_VULKAN
TEST_P(CudaGlInteropFormatTest, VulkanImageCanAliasAGlTexture) {
  const auto &expected = GetParam();
  VkExternalMemoryImageCreateInfo external_info = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};

  const auto info = cuda::shared_plane_image_info(expected.gl_format, 640, 360, 0, external_info);

  EXPECT_EQ(info.sType, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
  EXPECT_EQ(info.pNext, &external_info);
  EXPECT_EQ(info.format, expected.vk_format);
  EXPECT_EQ(cuda::shared_plane_vk_format(expected.gl_format), expected.vk_format);
  EXPECT_EQ(info.imageType, VK_IMAGE_TYPE_2D);
  EXPECT_EQ(info.extent.width, 640u);
  EXPECT_EQ(info.extent.height, 360u);
  EXPECT_EQ(info.extent.depth, 1u);
  EXPECT_EQ(info.mipLevels, 1u);
  EXPECT_EQ(info.arrayLayers, 1u);
  EXPECT_EQ(info.samples, VK_SAMPLE_COUNT_1_BIT);
  EXPECT_EQ(info.tiling, VK_IMAGE_TILING_OPTIMAL);

  // EXT_external_objects issue 17.
  EXPECT_EQ(info.flags, static_cast<VkImageCreateFlags>(VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT));

  // EXT_external_objects issue 13: every usage the format supports.
  const VkImageUsageFlags always = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
  EXPECT_EQ(info.usage, always);

  const auto with_storage = cuda::shared_plane_image_info(expected.gl_format, 640, 360, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, external_info);
  EXPECT_EQ(with_storage.usage, always | VK_IMAGE_USAGE_STORAGE_BIT);
}

TEST(CudaGlInteropTest, UnknownFormatHasNoVulkanFormat) {
  EXPECT_EQ(cuda::shared_plane_vk_format(GL_RGBA8), VK_FORMAT_UNDEFINED);
}
    #endif

INSTANTIATE_TEST_SUITE_P(PlaneFormats, CudaGlInteropFormatTest, testing::ValuesIn(plane_format_cases));

TEST_P(CudaGlInteropFailureTest, SecondPlaneFailureReleasesEverythingStartedOnce) {
  shared_planes_fixture_t f;
  f.state->failing_step = GetParam();
  f.state->failing_plane = 1;
  ASSERT_EQ(f.planes.init(), 0);
  ASSERT_EQ(f.planes.allocate(0, GL_R8, 64, 32), 0);

  EXPECT_EQ(f.planes.allocate(1, GL_RG8, 32, 16), -1);

  // Nothing leaks, whoever ended up holding each descriptor.
  EXPECT_TRUE(f.state->open_fds.empty());
  for (const auto &[fd, imports] : f.state->imported_fds) {
    EXPECT_EQ(imports, 1u) << "descriptor " << fd;
  }

  // The steps after the failed one never ran.
  constexpr std::array steps {"allocate_memory", "export_fd", "import_gl", "create_gl_storage", "import_cuda", "map_array"};
  const auto failed = std::ranges::find(steps, GetParam()) - steps.begin();
  for (const auto *later : steps | std::views::drop(failed + 1)) {
    EXPECT_EQ(f.state->count(std::string(later) + ":1"), 0) << later;
  }

  const auto calls_before_reset = f.state->calls;
  f.planes.reset();
  f.planes.reset();

  // The first plane is released completely.
  for (const auto *step : release_steps) {
    EXPECT_EQ(f.state->count(std::string(step) + ":0"), 1) << step;
  }

  // The second plane releases exactly what it started.
  const std::map<std::string, std::string> acquired_by {
    {"release_array", "map_array:1"},
    {"release_cuda_memory", "import_cuda:1"},
    {"release_gl", "import_gl:1"},
    {"free_memory", "allocate_memory:1"},
  };
  for (const auto *step : release_steps) {
    const bool started = std::ranges::count(calls_before_reset, acquired_by.at(step)) == 1;
    EXPECT_EQ(f.state->count(std::string(step) + ":1"), started ? 1 : 0) << step;
  }

  // CUDA goes before GL and Vulkan, and the device goes last.
  EXPECT_LT(f.state->position("free_memory:1"), f.state->position("destroy_device"));
  if (f.state->count("release_cuda_memory:1")) {
    EXPECT_LT(f.state->position("release_cuda_memory:1"), f.state->position("release_gl:1"));
  }
  if (f.state->count("release_gl:1")) {
    EXPECT_LT(f.state->position("release_gl:1"), f.state->position("free_memory:1"));
  }
  EXPECT_EQ(f.state->count("destroy_event"), 1);
  EXPECT_EQ(f.state->count("destroy_device"), 1);
}

INSTANTIATE_TEST_SUITE_P(
  Steps,
  CudaGlInteropFailureTest,
  testing::Values("allocate_memory", "export_fd", "import_gl", "create_gl_storage", "import_cuda", "map_array")
);

TEST(CudaGlInteropTest, SecondExportFailureClosesTheFirstDescriptor) {
  shared_planes_fixture_t f;
  f.state->failing_step = "export_fd";
  f.state->failing_plane = 0;
  f.state->failing_export = 2;
  ASSERT_EQ(f.planes.init(), 0);

  EXPECT_EQ(f.planes.allocate(0, GL_R8, 64, 32), -1);

  EXPECT_EQ(f.state->count("close_fd"), 1);
  EXPECT_TRUE(f.state->open_fds.empty());
}

namespace {
  /**
   * @brief Fake steps of creating the plane textures, recording their order.
   */
  struct fake_target_t {
    std::set<std::string> failing_steps;  ///< Steps that fail.
    std::vector<std::string> calls;  ///< Steps in the order they ran.

    /**
     * @brief Target steps backed by this fake.
     *
     * @return The steps.
     */
    cuda::gl_cuda_target_ops_t ops() {
      const auto step = [this](const std::string &name) {
        return [this, name] {
          calls.push_back(name);
          return failing_steps.contains(name) ? -1 : 0;
        };
      };
      const auto void_step = [this](const std::string &name) {
        return [this, name] {
          calls.push_back(name);
        };
      };

      return {
        .init_shared = step("init_shared"),
        .create_shared_target = step("create_shared_target"),
        .drop_target = void_step("drop_target"),
        .reset_shared = void_step("reset_shared"),
        .create_mapped_target = step("create_mapped_target"),
        .register_mapped_target = step("register_mapped_target"),
      };
    }
  };
}  // namespace

TEST(CudaGlInteropTest, SharedTargetNeedsNoFallback) {
  fake_target_t fake;

  EXPECT_EQ(cuda::create_gl_cuda_target(fake.ops()), 0);
  EXPECT_EQ(fake.calls, (std::vector<std::string> {"init_shared", "create_shared_target"}));
}

TEST(CudaGlInteropTest, InitFailureFallsBackToMappedTarget) {
  fake_target_t fake {.failing_steps = {"init_shared"}};

  EXPECT_EQ(cuda::create_gl_cuda_target(fake.ops()), 0);
  EXPECT_EQ(fake.calls, (std::vector<std::string> {"init_shared", "drop_target", "reset_shared", "create_mapped_target", "register_mapped_target"}));
}

TEST(CudaGlInteropTest, SharedTargetFailureDropsTexturesBeforeTheirMemoryAndFallsBack) {
  fake_target_t fake {.failing_steps = {"create_shared_target"}};

  EXPECT_EQ(cuda::create_gl_cuda_target(fake.ops()), 0);
  EXPECT_EQ(fake.calls, (std::vector<std::string> {"init_shared", "create_shared_target", "drop_target", "reset_shared", "create_mapped_target", "register_mapped_target"}));
}

TEST(CudaGlInteropTest, RealPlaneFailureReleasesItsMemoryAfterDroppingTexturesAndFallsBack) {
  shared_planes_fixture_t f;
  f.state->failing_step = "import_cuda";
  f.state->failing_plane = 1;
  fake_target_t target;

  auto ops = target.ops();
  ops.init_shared = [&f] {
    return f.planes.init();
  };
  ops.create_shared_target = [&f] {
    return f.planes.allocate(0, GL_R8, 64, 32) || f.planes.allocate(1, GL_RG8, 32, 16) ? -1 : 0;
  };
  ops.drop_target = [&f, &target] {
    // Nothing backing the textures may be gone yet.
    EXPECT_EQ(f.state->count("free_memory:0"), 0);
    EXPECT_EQ(f.state->count("destroy_device"), 0);
    target.calls.emplace_back("drop_target");
  };
  ops.reset_shared = [&f, &target] {
    f.planes.reset();
    target.calls.emplace_back("reset_shared");
  };

  EXPECT_EQ(cuda::create_gl_cuda_target(ops), 0);

  EXPECT_FALSE(f.planes.is_active());
  EXPECT_TRUE(f.state->open_fds.empty());
  EXPECT_EQ(f.state->count("free_memory:0"), 1);
  EXPECT_EQ(f.state->count("free_memory:1"), 1);
  EXPECT_EQ(f.state->count("destroy_device"), 1);
  EXPECT_EQ(target.calls, (std::vector<std::string> {"drop_target", "reset_shared", "create_mapped_target", "register_mapped_target"}));
}

TEST(CudaGlInteropTest, MappedTargetFailureSkipsRegistration) {
  fake_target_t fake {.failing_steps = {"init_shared", "create_mapped_target"}};

  EXPECT_EQ(cuda::create_gl_cuda_target(fake.ops()), -1);
  EXPECT_EQ(fake.calls, (std::vector<std::string> {"init_shared", "drop_target", "reset_shared", "create_mapped_target"}));
}

TEST(CudaGlInteropTest, RegistrationFailureIsReported) {
  fake_target_t fake {.failing_steps = {"init_shared", "register_mapped_target"}};

  EXPECT_EQ(cuda::create_gl_cuda_target(fake.ops()), -1);
}
  #endif  // SUNSHINE_BUILD_CUDA

#endif  // __linux__
