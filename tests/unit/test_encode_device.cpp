/**
 * @file tests/unit/test_encode_device.cpp
 * @brief Tests for typed AVCodec hardware device initialization.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <type_traits>

// local includes
#include <src/audio.h>
#include <src/video.h>

namespace video {
  util::Either<avcodec_buffer_t, int> vaapi_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *encode_device);
#ifdef SUNSHINE_BUILD_VULKAN
  util::Either<avcodec_buffer_t, int> vulkan_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *encode_device);
#endif
}  // namespace video

namespace {
  /**
   * @brief Fake backend that records initialization and supplies an FFmpeg buffer.
   */
  struct tracked_encode_device_t: platf::avcodec_encode_device_t {
    int calls = 0;  ///< Number of backend initialization calls.
    int status = 0;  ///< Status returned by the fake backend.
    AVBufferRef *created_buffer = nullptr;  ///< Non-owning identity of the buffer returned to FFmpeg.

    tracked_encode_device_t() {
      hardware = true;
      init_hw_device = &initialize;
    }

    /**
     * @brief Initialize a tracked hardware buffer or return the configured error.
     * @param device Fake encode device whose invocation is recorded.
     * @param buffer Receives the hardware buffer on success.
     * @return Configured failure status or zero on success.
     */
    static int initialize(platf::avcodec_encode_device_t *device, AVBufferRef **buffer) {
      auto *tracked = static_cast<tracked_encode_device_t *>(device);
      ++tracked->calls;
      if (tracked->status) {
        return tracked->status;
      }
      tracked->created_buffer = av_buffer_alloc(1);
      *buffer = tracked->created_buffer;
      return *buffer ? 0 : AVERROR(ENOMEM);
    }
  };

  /**
   * @brief Supported hardware initialization entry points under test.
   */
  using initializer_t = util::Either<video::avcodec_buffer_t, int> (*)(platf::avcodec_encode_device_t *);

  /**
   * @brief Hardware initialization entry points available in this build.
   */
  constexpr std::array hardware_initializers {
    &video::vaapi_init_avcodec_hardware_input_buffer,
#ifdef SUNSHINE_BUILD_VULKAN
    &video::vulkan_init_avcodec_hardware_input_buffer,
#endif
  };

  /**
   * @brief Exercise the same typed backend contract for each hardware entry point.
   */
  struct EncodeDeviceInitializationTest: testing::TestWithParam<initializer_t> {};
}  // namespace

static_assert(std::is_same_v<audio::packet_t::first_type, stream::session_t *>);
static_assert(std::is_same_v<decltype(video::packet_raw_t::channel_data), stream::session_t *>);

TEST(EncodeDeviceStateTest, SoftwareDeviceDoesNotAdvertiseHardwareResources) {
  platf::avcodec_encode_device_t device;
  EXPECT_FALSE(device.hardware);
  EXPECT_EQ(device.init_hw_device, nullptr);
#ifdef _WIN32
  EXPECT_EQ(device.d3d11_device, nullptr);
#endif
}

TEST_P(EncodeDeviceInitializationTest, UsesBackendBufferWithoutOpeningAnotherDevice) {
  tracked_encode_device_t device;
  auto result = GetParam()(&device);
  ASSERT_TRUE(result.has_left());
  ASSERT_NE(result.left().get(), nullptr);
  EXPECT_EQ(result.left().get(), device.created_buffer);
  EXPECT_EQ(device.calls, 1);
}

TEST_P(EncodeDeviceInitializationTest, PropagatesBackendFailureWithoutUsingFallback) {
  tracked_encode_device_t device;
  device.status = AVERROR(EINVAL);
  auto result = GetParam()(&device);
  ASSERT_FALSE(result.has_left());
  EXPECT_EQ(result.right(), -1);
  EXPECT_EQ(device.created_buffer, nullptr);
  EXPECT_EQ(device.calls, 1);
}

INSTANTIATE_TEST_SUITE_P(
  HardwareBackends,
  EncodeDeviceInitializationTest,
  testing::ValuesIn(hardware_initializers)
);
