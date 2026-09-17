/**
 * @file tests/unit/platform/linux/test_audio.cpp
 * @brief Tests for the Linux audio capture channel map.
 */

#ifdef __linux__

  // standard includes
  #include <vector>

  // lib includes
  #include <gtest/gtest.h>
  #include <pulse/pulseaudio.h>

  // local includes
  #include "src/platform/common.h"

namespace platf {
  bool fill_canonical_channel_map(int channels, pa_channel_map &pa_map);
}  // namespace platf

namespace {
  /**
   * @brief Collect the speaker positions a channel map describes.
   *
   * @param pa_map Channel map to read.
   * @return The positions of its channels, in channel order.
   */
  std::vector<pa_channel_position_t> positions_of(const pa_channel_map &pa_map) {
    return {pa_map.map, pa_map.map + pa_map.channels};
  }
}  // namespace

/**
 * @brief Capture must use the canonical layout, whatever Opus mapping the client asked for.
 *
 * The Opus mapping is applied by the multistream encoder and undone by the client when it
 * decodes, so applying it to the captured PCM as well leaves the client rendering permuted
 * channels. The webOS client requests 0,1,4,5,2,3, which would put the centre channel on the
 * rear left speaker.
 */
TEST(LinuxAudioChannelMap, Surround51UsesCanonicalOrder) {
  pa_channel_map pa_map;
  ASSERT_TRUE(platf::fill_canonical_channel_map(6, pa_map));

  const std::vector<pa_channel_position_t> expected {
    PA_CHANNEL_POSITION_FRONT_LEFT,
    PA_CHANNEL_POSITION_FRONT_RIGHT,
    PA_CHANNEL_POSITION_FRONT_CENTER,
    PA_CHANNEL_POSITION_LFE,
    PA_CHANNEL_POSITION_REAR_LEFT,
    PA_CHANNEL_POSITION_REAR_RIGHT
  };
  EXPECT_EQ(positions_of(pa_map), expected);
}

/**
 * @brief Stereo capture maps onto the two front channels.
 */
TEST(LinuxAudioChannelMap, StereoUsesCanonicalOrder) {
  pa_channel_map pa_map;
  ASSERT_TRUE(platf::fill_canonical_channel_map(2, pa_map));

  const std::vector<pa_channel_position_t> expected {
    PA_CHANNEL_POSITION_FRONT_LEFT,
    PA_CHANNEL_POSITION_FRONT_RIGHT
  };
  EXPECT_EQ(positions_of(pa_map), expected);
}

/**
 * @brief 7.1 capture adds the two side channels to the 5.1 layout.
 */
TEST(LinuxAudioChannelMap, Surround71UsesCanonicalOrder) {
  pa_channel_map pa_map;
  ASSERT_TRUE(platf::fill_canonical_channel_map(8, pa_map));

  const std::vector<pa_channel_position_t> expected {
    PA_CHANNEL_POSITION_FRONT_LEFT,
    PA_CHANNEL_POSITION_FRONT_RIGHT,
    PA_CHANNEL_POSITION_FRONT_CENTER,
    PA_CHANNEL_POSITION_LFE,
    PA_CHANNEL_POSITION_REAR_LEFT,
    PA_CHANNEL_POSITION_REAR_RIGHT,
    PA_CHANNEL_POSITION_SIDE_LEFT,
    PA_CHANNEL_POSITION_SIDE_RIGHT
  };
  EXPECT_EQ(positions_of(pa_map), expected);
}

/**
 * @brief The map the backend hands PulseAudio must be a valid one.
 */
TEST(LinuxAudioChannelMap, ProducesValidPulseAudioMaps) {
  for (const int channels : {2, 6, 8}) {
    pa_channel_map pa_map;
    ASSERT_TRUE(platf::fill_canonical_channel_map(channels, pa_map)) << "channels: " << channels;
    EXPECT_TRUE(pa_channel_map_valid(&pa_map)) << "channels: " << channels;
  }
}

/**
 * @brief Channel counts with no known speaker layout are rejected rather than read out of bounds.
 */
TEST(LinuxAudioChannelMap, RejectsUnsupportedChannelCounts) {
  pa_channel_map pa_map;
  EXPECT_FALSE(platf::fill_canonical_channel_map(0, pa_map));
  EXPECT_FALSE(platf::fill_canonical_channel_map(-1, pa_map));
  EXPECT_FALSE(platf::fill_canonical_channel_map(platf::speaker::MAX_SPEAKERS + 1, pa_map));
}

#endif  // __linux__
