/**
 * @file tests/unit/test_video_packets.cpp
 * @brief Test first-IDR packet collection without an encoder or display device.
 */
#include "../tests_common.h"

#include <algorithm>
#include <initializer_list>
#include <src/video.h>
#include <vector>

namespace {
  /**
   * @brief Create an owned FFmpeg packet with recognizable payload bytes.
   * @param bytes Payload copied into the packet.
   * @return Packet, or null if allocation fails.
   */
  std::unique_ptr<video::packet_raw_avcodec> make_packet(std::initializer_list<uint8_t> bytes) {
    auto packet = std::make_unique<video::packet_raw_avcodec>();
    if (!packet->av_packet || av_new_packet(packet->av_packet, static_cast<int>(bytes.size()))) {
      return nullptr;
    }
    std::copy(bytes.begin(), bytes.end(), packet->av_packet->data);
    return packet;
  }

  /**
   * @brief Exercise collection using real FFmpeg packets and caller-owned state.
   */
  struct FirstIdrPacketTest: testing::Test {
    uint8_t remaining = 0;  ///< Packets still required to complete the first IDR.
    std::vector<std::unique_ptr<video::packet_raw_avcodec>> cached;  ///< Packets retained between calls.
  };
}  // namespace

TEST_F(FirstIdrPacketTest, PassesThroughWhenCollectionIsDisabled) {
  auto packet = make_packet({11, 12});
  ASSERT_NE(packet, nullptr);
  auto *original = packet.get();
  auto *original_data = packet->data();

  EXPECT_EQ(video::collect_first_idr_packet(remaining, cached, packet), 0);
  ASSERT_NE(packet, nullptr);
  EXPECT_EQ(packet.get(), original);
  EXPECT_EQ(packet->data(), original_data);
  EXPECT_EQ(remaining, 0);
  EXPECT_TRUE(cached.empty());
}

TEST_F(FirstIdrPacketTest, StashesUntilCompleteAndPreservesReceiveOrder) {
  remaining = 3;
  auto packet = make_packet({11, 12});
  ASSERT_NE(packet, nullptr);
  auto *first = packet.get();
  ASSERT_EQ(video::collect_first_idr_packet(remaining, cached, packet), 1);
  EXPECT_EQ(packet, nullptr);
  EXPECT_EQ(remaining, 2);
  ASSERT_EQ(cached.size(), 1);
  EXPECT_EQ(cached.front().get(), first);

  packet = make_packet({21});
  ASSERT_NE(packet, nullptr);
  ASSERT_EQ(video::collect_first_idr_packet(remaining, cached, packet), 1);
  EXPECT_EQ(packet, nullptr);
  EXPECT_EQ(remaining, 1);
  ASSERT_EQ(cached.size(), 2);

  packet = make_packet({31, 32});
  ASSERT_NE(packet, nullptr);
  ASSERT_EQ(video::collect_first_idr_packet(remaining, cached, packet), 0);
  ASSERT_NE(packet, nullptr);
  EXPECT_EQ(remaining, 0);
  EXPECT_TRUE(cached.empty());
  EXPECT_EQ((std::vector<uint8_t> {packet->data(), packet->data() + packet->data_size()}), (std::vector<uint8_t> {11, 12, 21, 31, 32}));

  packet = make_packet({41, 42});
  ASSERT_NE(packet, nullptr);
  auto *subsequent_data = packet->data();
  EXPECT_EQ(video::collect_first_idr_packet(remaining, cached, packet), 0);
  ASSERT_NE(packet, nullptr);
  EXPECT_EQ(packet->data(), subsequent_data);
  EXPECT_EQ((std::vector<uint8_t> {packet->data(), packet->data() + packet->data_size()}), (std::vector<uint8_t> {41, 42}));
  EXPECT_TRUE(cached.empty());
}

TEST_F(FirstIdrPacketTest, CopiesPropertiesFromTheFinalPacket) {
  remaining = 2;
  auto packet = make_packet({11});
  ASSERT_NE(packet, nullptr);
  packet->av_packet->pts = 1;
  packet->av_packet->flags = AV_PKT_FLAG_DISCARD;
  ASSERT_EQ(video::collect_first_idr_packet(remaining, cached, packet), 1);
  ASSERT_EQ(packet, nullptr);

  packet = make_packet({21, 22});
  ASSERT_NE(packet, nullptr);
  packet->av_packet->flags = AV_PKT_FLAG_KEY;
  packet->av_packet->pts = 100;
  packet->av_packet->dts = 99;
  packet->av_packet->duration = 7;
  auto *side_data = av_packet_new_side_data(packet->av_packet, AV_PKT_DATA_STRINGS_METADATA, 3);
  ASSERT_NE(side_data, nullptr);
  side_data[0] = 'x';
  side_data[1] = 0;
  side_data[2] = 0;

  ASSERT_EQ(video::collect_first_idr_packet(remaining, cached, packet), 0);
  ASSERT_NE(packet, nullptr);
  EXPECT_EQ((std::vector<uint8_t> {packet->data(), packet->data() + packet->data_size()}), (std::vector<uint8_t> {11, 21, 22}));
  EXPECT_EQ(packet->av_packet->flags, AV_PKT_FLAG_KEY);
  EXPECT_TRUE(packet->is_idr());
  EXPECT_EQ(packet->frame_index(), 100);
  EXPECT_EQ(packet->av_packet->dts, 99);
  EXPECT_EQ(packet->av_packet->duration, 7);
  size_t side_data_size = 0;
  auto *copied_side_data = av_packet_get_side_data(packet->av_packet, AV_PKT_DATA_STRINGS_METADATA, &side_data_size);
  ASSERT_NE(copied_side_data, nullptr);
  ASSERT_EQ(side_data_size, 3);
  EXPECT_EQ(copied_side_data[0], 'x');
  EXPECT_EQ(copied_side_data[1], 0);
  EXPECT_EQ(copied_side_data[2], 0);
  EXPECT_EQ(remaining, 0);
  EXPECT_TRUE(cached.empty());
}
