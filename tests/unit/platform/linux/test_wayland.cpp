/**
 * @file tests/unit/platform/linux/test_wayland.cpp
 * @brief Test Wayland output mode selection.
 */
#ifdef SUNSHINE_BUILD_WAYLAND
  // standard includes
  #include <array>
  #include <cerrno>
  #include <chrono>

  // system includes
  #include <drm_fourcc.h>
  #include <fcntl.h>
  #include <unistd.h>

  // test includes
  #include "../../../tests_common.h"

  // local includes
  #include <src/platform/linux/wayland.h>

namespace {
  struct fake_gbm_bo_t {
    int plane_count {};
    int failed_plane {-1};
    std::uint64_t modifier {};
    std::array<std::uint32_t, 4> strides {};
    std::array<std::uint32_t, 4> offsets {};
    std::array<int, 4> exported_fds {-1, -1, -1, -1};
  };

  fake_gbm_bo_t &fake_bo(gbm_bo *bo) {
    return *static_cast<fake_gbm_bo_t *>(static_cast<void *>(bo));
  }

  int get_plane_count(gbm_bo *bo) {
    return fake_bo(bo).plane_count;
  }

  int get_fd_for_plane(gbm_bo *bo, int plane) {
    auto &fake = fake_bo(bo);
    if (plane == fake.failed_plane) {
      return -1;
    }

    fake.exported_fds[plane] = open("/dev/null", O_RDONLY | O_CLOEXEC);
    return fake.exported_fds[plane];
  }

  std::uint32_t get_stride_for_plane(gbm_bo *bo, int plane) {
    return fake_bo(bo).strides[plane];
  }

  std::uint32_t get_offset(gbm_bo *bo, int plane) {
    return fake_bo(bo).offsets[plane];
  }

  std::uint64_t get_modifier(gbm_bo *bo) {
    return fake_bo(bo).modifier;
  }

  const wl::gbm_bo_accessors_t fake_accessors {
    .get_plane_count = get_plane_count,
    .get_fd_for_plane = get_fd_for_plane,
    .get_stride_for_plane = get_stride_for_plane,
    .get_offset = get_offset,
    .get_modifier = get_modifier,
  };
}  // namespace

TEST(WaylandMonitorTest, IgnoresNonCurrentModesAroundCurrentMode) {
  wl::monitor_t monitor {nullptr};

  monitor.wl_mode(nullptr, WL_OUTPUT_MODE_PREFERRED, 1280, 720, 60000);
  monitor.wl_mode(nullptr, WL_OUTPUT_MODE_CURRENT, 1920, 1080, 60000);
  monitor.wl_mode(nullptr, 0, 2560, 1440, 60000);

  EXPECT_EQ(monitor.viewport.width, 1920);
  EXPECT_EQ(monitor.viewport.height, 1080);
}

TEST(WaylandMonitorTest, UpdatesCurrentMode) {
  wl::monitor_t monitor {nullptr};

  monitor.wl_mode(nullptr, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, 1920, 1080, 60000);
  monitor.wl_mode(nullptr, WL_OUTPUT_MODE_CURRENT, 2560, 1440, 120000);

  EXPECT_EQ(monitor.viewport.width, 2560);
  EXPECT_EQ(monitor.viewport.height, 1440);
}

TEST(WaylandMonitorTest, RecordsRefreshOfCurrentMode) {
  wl::monitor_t monitor {nullptr};

  EXPECT_EQ(monitor.refresh_mhz, 0);

  monitor.wl_mode(nullptr, WL_OUTPUT_MODE_PREFERRED, 1920, 1080, 60000);
  EXPECT_EQ(monitor.refresh_mhz, 0);

  monitor.wl_mode(nullptr, WL_OUTPUT_MODE_CURRENT, 1920, 1080, 120000);
  monitor.wl_mode(nullptr, 0, 1920, 1080, 144000);
  EXPECT_EQ(monitor.refresh_mhz, 120000);
}

TEST(WaylandCaptureTest, UsesDamageCaptureOnlyOnAFasterOutput) {
  // An output at twice the stream's rate, and one just at the threshold.
  EXPECT_TRUE(wl::use_damage_capture(3, 60.0, 120000));
  EXPECT_TRUE(wl::use_damage_capture(3, 60.0, 90000));

  // An output at the stream's own rate keeps the paced copy, as does one only slightly faster.
  EXPECT_FALSE(wl::use_damage_capture(3, 60.0, 60000));
  EXPECT_FALSE(wl::use_damage_capture(3, 60.0, 89999));
  EXPECT_FALSE(wl::use_damage_capture(3, 120.0, 120000));
}

TEST(WaylandCaptureTest, KeepsPacedCaptureWithoutProtocolOrRates) {
  // copy_with_damage arrived in version 2 of the protocol.
  EXPECT_FALSE(wl::use_damage_capture(1, 60.0, 120000));
  EXPECT_TRUE(wl::use_damage_capture(2, 60.0, 120000));

  // A compositor that never reported a mode, and a stream without a rate.
  EXPECT_FALSE(wl::use_damage_capture(3, 60.0, 0));
  EXPECT_FALSE(wl::use_damage_capture(3, 0.0, 120000));
}

TEST(WaylandCaptureTest, DamageRequestBudgetFollowsASourceAtTheStreamRate) {
  using namespace std::chrono_literals;
  constexpr std::chrono::nanoseconds delay = 16ms;
  const std::chrono::steady_clock::time_point start {1s};

  // Frames one interval apart: the next request is always allowed a quarter interval before
  // the next frame is due, however long that goes on.
  auto next_request = std::chrono::steady_clock::time_point {};
  for (int i = 0; i < 100; ++i) {
    const auto frame_time = start + i * delay;
    next_request = wl::next_damage_request(next_request, frame_time, delay);
    EXPECT_EQ(next_request, frame_time + delay - delay / 4);
  }
}

TEST(WaylandCaptureTest, DamageRequestBudgetHoldsAFasterSourceToTheStreamRate) {
  using namespace std::chrono_literals;
  constexpr std::chrono::nanoseconds delay = 16ms;
  const std::chrono::steady_clock::time_point start {1s};

  // Frames arriving twice as fast as the stream: each one still costs a whole interval.
  auto next_request = wl::next_damage_request({}, start, delay);
  const auto first = next_request;
  for (int i = 1; i <= 10; ++i) {
    next_request = wl::next_damage_request(next_request, start + i * (delay / 2), delay);
  }
  EXPECT_EQ(next_request, first + 10 * delay);
}

TEST(WaylandCaptureTest, DamageRequestBudgetResetsAfterALateFrame) {
  using namespace std::chrono_literals;
  constexpr std::chrono::nanoseconds delay = 16ms;
  const std::chrono::steady_clock::time_point start {1s};

  // A still picture, then a frame a second later: no budget is carried over from the pause.
  auto next_request = wl::next_damage_request({}, start, delay);
  const auto late = start + 1s;
  next_request = wl::next_damage_request(next_request, late, delay);
  EXPECT_EQ(next_request, late + delay - delay / 4);
}

TEST(WaylandCaptureTest, WaitsOnlyForADamageRequestWithinOneInterval) {
  using namespace std::chrono_literals;
  constexpr std::chrono::nanoseconds delay = 16ms;
  const std::chrono::steady_clock::time_point now {1s};

  EXPECT_TRUE(wl::should_wait_for_damage_request(now + 4ms, now, delay));

  // Already due.
  EXPECT_FALSE(wl::should_wait_for_damage_request(now, now, delay));
  EXPECT_FALSE(wl::should_wait_for_damage_request(now - 4ms, now, delay));
  EXPECT_FALSE(wl::should_wait_for_damage_request({}, now, delay));

  // Further off than one interval: a budget gone wrong, not something to sleep on.
  EXPECT_FALSE(wl::should_wait_for_damage_request(now + delay, now, delay));
  EXPECT_FALSE(wl::should_wait_for_damage_request(now + 1h, now, delay));
}

TEST(WaylandCaptureTest, DamageFrameTimeTrustsOnlyATimestampFromItsOwnClock) {
  using namespace std::chrono_literals;
  constexpr std::chrono::nanoseconds delay = 16ms;
  const std::chrono::steady_clock::time_point now {2h};

  // Presented a moment before it was received.
  EXPECT_EQ(wl::damage_frame_time(now - 1ms, now, delay), now - 1ms);
  EXPECT_EQ(wl::damage_frame_time(now, now, delay), now);

  // Ahead of the clock, a whole interval or more behind it, or missing.
  EXPECT_EQ(wl::damage_frame_time(now + 1ms, now, delay), now);
  EXPECT_EQ(wl::damage_frame_time(now + 1h, now, delay), now);
  EXPECT_EQ(wl::damage_frame_time(now - delay, now, delay), now);
  EXPECT_EQ(wl::damage_frame_time(now - 1h, now, delay), now);
  EXPECT_EQ(wl::damage_frame_time(std::nullopt, now, delay), now);
}

TEST(WaylandCaptureTest, DamageRequestBudgetCapsTheRateWhateverClockTheCompositorUses) {
  using namespace std::chrono_literals;
  constexpr std::chrono::nanoseconds delay = 10ms;
  constexpr std::chrono::nanoseconds source_interval = delay / 2;
  const std::chrono::steady_clock::time_point start {2h};

  // A source at twice the stream's rate, captured for one second the way the capture loop does
  // it: wait for the budget, ask, and take the source's next frame.
  for (const auto clock_offset : std::initializer_list<std::chrono::nanoseconds> {0ns, 3ms, -3ms, 1s, -1s, 1h, -1h}) {
    auto next_request = std::chrono::steady_clock::time_point {};
    auto now = start;
    int frames = 0;

    while (now < start + 1s) {
      if (wl::should_wait_for_damage_request(next_request, now, delay)) {
        now = next_request;
      }

      const auto since_start = now - start;
      const auto ticks = (since_start + source_interval - 1ns) / source_interval;
      now = start + ticks * source_interval;

      const auto frame_time = wl::damage_frame_time(now + clock_offset, now, delay);
      next_request = wl::next_damage_request(next_request, frame_time, delay);
      ++frames;

      // The source has moved on by the time the next frame is asked for.
      now += 1ns;
    }

    // One hundred for the second, the frame that opens it, and at most one more at the start
    // from a timestamp that lags by less than an interval.
    EXPECT_GE(frames, 100) << "clock offset " << clock_offset.count() << " ns";
    EXPECT_LE(frames, 102) << "clock offset " << clock_offset.count() << " ns";
  }
}

TEST(WaylandCaptureTest, AsksForTheFirstFrameOutrightAndThenForChanges) {
  using enum wl::screencopy_request_e;

  EXPECT_EQ(wl::next_screencopy_request(false, false, true, true, false), copy);
  EXPECT_EQ(wl::next_screencopy_request(false, true, true, true, true), copy_with_damage);

  // Paced capture asks outright every time.
  EXPECT_EQ(wl::next_screencopy_request(false, true, true, false, true), copy);
}

TEST(WaylandCaptureTest, ReplacesAPendingRequestWhenTheCursorIsToggledOnAStillPicture) {
  using enum wl::screencopy_request_e;

  // A still picture: the request for the next change outlives one timeout after another.
  EXPECT_EQ(wl::next_screencopy_request(true, true, true, true, true), keep);
  EXPECT_EQ(wl::next_screencopy_request(true, true, true, true, true), keep);

  // The cursor is switched off, which damages nothing. The pending request would still answer
  // with the cursor in the picture, so it gives way to a plain copy.
  EXPECT_EQ(wl::next_screencopy_request(true, true, false, true, true), copy);

  // That copy was made with the new setting and is kept in turn, then changes are asked for again.
  EXPECT_EQ(wl::next_screencopy_request(true, false, false, true, true), keep);
  EXPECT_EQ(wl::next_screencopy_request(false, false, false, true, true), copy_with_damage);

  // And back on.
  EXPECT_EQ(wl::next_screencopy_request(true, false, true, true, true), copy);

  // Paced capture replaces a request that did not answer in time the same way.
  EXPECT_EQ(wl::next_screencopy_request(true, true, false, false, true), copy);
}

TEST(WaylandCaptureTest, AsksOutrightWhenTheCursorIsToggledBetweenRequests) {
  using enum wl::screencopy_request_e;

  // A frame with the cursor was delivered and nothing is pending. The cursor is switched off
  // before the next request is made. Nothing on the output changes, so a request for the next
  // change would wait while the stream goes on showing the cursor.
  EXPECT_EQ(wl::next_screencopy_request(false, true, false, true, true), copy);

  // That copy was made without the cursor. The picture is up to date, changes are asked for
  // again and the request is kept over timeouts.
  EXPECT_EQ(wl::next_screencopy_request(false, false, false, true, true), copy_with_damage);
  EXPECT_EQ(wl::next_screencopy_request(true, false, false, true, true), keep);

  // And back on.
  EXPECT_EQ(wl::next_screencopy_request(false, false, true, true, true), copy);
}

TEST(WaylandCaptureTest, AsksOutrightAfterARequestThatCouldNotBeWithdrawn) {
  using enum wl::screencopy_request_e;

  // The cursor is switched off while a request made with it is out. It is to be replaced, but
  // its buffer is still being set up and it can not be withdrawn. The caller keeps it and leaves
  // the setting it remembers alone, because no request was made with the new one.
  bool requested_cursor = true;
  EXPECT_EQ(wl::next_screencopy_request(true, requested_cursor, false, true, true), copy);

  // The old request is answered, with the cursor in the picture. The next request has to bring
  // the picture up to date, not wait for a change.
  EXPECT_EQ(wl::next_screencopy_request(false, requested_cursor, false, true, true), copy);

  // Only that request was made with the new setting.
  requested_cursor = false;
  EXPECT_EQ(wl::next_screencopy_request(false, requested_cursor, false, true, true), copy_with_damage);
}

TEST(WaylandCaptureTest, UsesVramForVaapi) {
  EXPECT_TRUE(wl::use_vram_capture(platf::mem_type_e::vaapi));
}

TEST(WaylandCaptureTest, UsesSystemMemoryForSoftwareEncoding) {
  EXPECT_FALSE(wl::use_vram_capture(platf::mem_type_e::system));
}

TEST(WaylandCaptureTest, UsesVramForCudaOnlyWhenCudaSupportIsBuilt) {
  #ifdef SUNSHINE_BUILD_CUDA
  EXPECT_TRUE(wl::use_vram_capture(platf::mem_type_e::cuda));
  #else
  EXPECT_FALSE(wl::use_vram_capture(platf::mem_type_e::cuda));
  #endif
}

TEST(WaylandInterfaceTest, RecordsOnlyExplicitDmabufModifiers) {
  constexpr std::uint32_t format = DRM_FORMAT_XRGB8888;
  constexpr std::uint64_t explicit_modifier = 0x100000000000004;
  wl::interface_t interface;

  interface.dmabuf_modifier(nullptr, format, DRM_FORMAT_MOD_INVALID >> 32, DRM_FORMAT_MOD_INVALID & 0xffffffff);
  interface.dmabuf_modifier(nullptr, format, explicit_modifier >> 32, explicit_modifier & 0xffffffff);

  const auto modifiers = interface.supported_modifiers.find(format);
  ASSERT_NE(modifiers, interface.supported_modifiers.end());
  ASSERT_EQ(modifiers->second.size(), 1);
  EXPECT_EQ(modifiers->second.front(), explicit_modifier);
}

TEST(WaylandCaptureTest, ExportsEveryGbmBufferPlane) {
  fake_gbm_bo_t bo {
    .plane_count = 2,
    .modifier = 0x100000000000004,
    .strides = {10240, 256},
    .offsets = {0, 0x800000},
  };
  wl::frame_t frame;

  const auto plane_count = wl::export_gbm_bo_planes(static_cast<gbm_bo *>(static_cast<void *>(&bo)), frame, fake_accessors);

  ASSERT_EQ(plane_count, 2);
  EXPECT_EQ(frame.sd.modifier, bo.modifier);
  for (std::size_t plane = 0; plane < *plane_count; ++plane) {
    EXPECT_EQ(frame.sd.fds[plane], bo.exported_fds[plane]);
    EXPECT_EQ(frame.sd.pitches[plane], bo.strides[plane]);
    EXPECT_EQ(frame.sd.offsets[plane], bo.offsets[plane]);
  }
  EXPECT_EQ(frame.sd.fds[2], -1);
  EXPECT_EQ(frame.sd.fds[3], -1);

  frame.destroy();
}

TEST(WaylandCaptureTest, RejectsUnsupportedGbmPlaneCounts) {
  wl::frame_t frame;
  fake_gbm_bo_t empty_bo {};
  fake_gbm_bo_t oversized_bo {
    .plane_count = 5,
  };

  EXPECT_FALSE(wl::export_gbm_bo_planes(static_cast<gbm_bo *>(static_cast<void *>(&empty_bo)), frame, fake_accessors));
  EXPECT_FALSE(wl::export_gbm_bo_planes(static_cast<gbm_bo *>(static_cast<void *>(&oversized_bo)), frame, fake_accessors));
}

TEST(WaylandCaptureTest, ClosesExportedPlanesAfterLaterPlaneFails) {
  fake_gbm_bo_t bo {
    .plane_count = 2,
    .failed_plane = 1,
  };
  wl::frame_t frame;

  EXPECT_FALSE(wl::export_gbm_bo_planes(static_cast<gbm_bo *>(static_cast<void *>(&bo)), frame, fake_accessors));
  ASSERT_GE(bo.exported_fds[0], 0);
  const auto fd_status = fcntl(bo.exported_fds[0], F_GETFD);
  const auto fd_error = errno;
  EXPECT_EQ(fd_status, -1);
  EXPECT_EQ(fd_error, EBADF);
  EXPECT_EQ(frame.sd.fds[0], -1);
}

namespace {
  /**
   * @brief Count of fake GBM devices destroyed by the accessors below (function-local state, reset per test).
   */
  int &fake_destroyed_devices() {
    static int count = 0;
    return count;
  }

  /**
   * @brief Descriptor most recently returned by open_fake_render_node() (reset per test).
   */
  int &last_fake_render_node() {
    static int fd = -1;
    return fd;
  }

  /**
   * @brief Count of create_device calls made through the accessors below (reset per test).
   */
  int &fake_create_device_calls() {
    static int count = 0;
    return count;
  }

  int open_fake_render_node(const char *) {
    last_fake_render_node() = open("/dev/null", O_RDWR | O_CLOEXEC);
    return last_fake_render_node();
  }

  int fail_to_open_render_node(const char *) {
    return -1;
  }

  gbm_device *create_fake_device(int fd) {
    ++fake_create_device_calls();
    // The address of this object stands in for an opaque GBM device.
    static int storage = 0;
    return fd >= 0 ? static_cast<gbm_device *>(static_cast<void *>(&storage)) : nullptr;
  }

  gbm_device *fail_to_create_device(int) {
    ++fake_create_device_calls();
    return nullptr;
  }

  void destroy_fake_device(gbm_device *) {
    ++fake_destroyed_devices();
  }

  bool descriptor_is_open(int fd) {
    return fcntl(fd, F_GETFD) != -1;
  }
}  // namespace

TEST(WaylandGbmDeviceTest, ClosesRenderNodeDescriptorOnReset) {
  const wl::gbm_device_accessors_t accessors {
    .open_render_node = open_fake_render_node,
    .create_device = create_fake_device,
    .destroy_device = destroy_fake_device,
  };
  fake_destroyed_devices() = 0;

  wl::gbm_device_t device;
  ASSERT_TRUE(device.init("/dev/dri/renderD128", accessors));
  ASSERT_TRUE(device);
  const int fd = device.fd();
  ASSERT_GE(fd, 0);
  EXPECT_TRUE(descriptor_is_open(fd));

  device.reset();
  EXPECT_FALSE(device);
  EXPECT_EQ(device.fd(), -1);
  EXPECT_EQ(fake_destroyed_devices(), 1);
  EXPECT_FALSE(descriptor_is_open(fd));
}

TEST(WaylandGbmDeviceTest, ClosesRenderNodeDescriptorWhenDeviceCreationFails) {
  const wl::gbm_device_accessors_t accessors {
    .open_render_node = open_fake_render_node,
    .create_device = fail_to_create_device,
    .destroy_device = destroy_fake_device,
  };
  fake_destroyed_devices() = 0;
  fake_create_device_calls() = 0;
  last_fake_render_node() = -1;

  wl::gbm_device_t device;
  EXPECT_FALSE(device.init("/dev/dri/renderD128", accessors));
  EXPECT_FALSE(device);
  EXPECT_EQ(device.fd(), -1);
  EXPECT_EQ(fake_create_device_calls(), 1);
  EXPECT_EQ(fake_destroyed_devices(), 0);

  // Check the descriptor the fake accessor actually opened.
  const int fd = last_fake_render_node();
  ASSERT_GE(fd, 0);
  EXPECT_FALSE(descriptor_is_open(fd));
}

TEST(WaylandGbmDeviceTest, DoesNotCreateDeviceWhenRenderNodeFailsToOpen) {
  const wl::gbm_device_accessors_t accessors {
    .open_render_node = fail_to_open_render_node,
    .create_device = create_fake_device,
    .destroy_device = destroy_fake_device,
  };
  fake_destroyed_devices() = 0;
  fake_create_device_calls() = 0;

  wl::gbm_device_t device;
  EXPECT_FALSE(device.init("/dev/dri/renderD128", accessors));
  EXPECT_FALSE(device);
  EXPECT_EQ(device.get(), nullptr);
  EXPECT_EQ(device.fd(), -1);
  EXPECT_EQ(fake_create_device_calls(), 0);
  EXPECT_EQ(fake_destroyed_devices(), 0);
}

TEST(WaylandGbmDeviceTest, DestructorClosesRenderNodeDescriptor) {
  const wl::gbm_device_accessors_t accessors {
    .open_render_node = open_fake_render_node,
    .create_device = create_fake_device,
    .destroy_device = destroy_fake_device,
  };
  fake_destroyed_devices() = 0;
  int fd = -1;

  {
    wl::gbm_device_t device;
    ASSERT_TRUE(device.init("/dev/dri/renderD128", accessors));
    fd = device.fd();
    ASSERT_TRUE(descriptor_is_open(fd));
  }

  EXPECT_EQ(fake_destroyed_devices(), 1);
  EXPECT_FALSE(descriptor_is_open(fd));
}

#endif
