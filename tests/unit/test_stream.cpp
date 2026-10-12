/**
 * @file tests/unit/test_stream.cpp
 * @brief Test src/stream.*
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// local includes
#include <src/network.h>
#include <src/stream_pacing.h>

namespace stream {
  std::vector<uint8_t> concat_and_insert(uint64_t insert_size, uint64_t slice_size, const std::string_view &data1, const std::string_view &data2);
  std::optional<std::pair<std::uint16_t, std::string_view>> parse_control_packet(const ENetPacket &packet);
}  // namespace stream

namespace {
  /**
   * @brief Record pacing requests without sleeping or depending on a native timer.
   */
  class recording_video_timer_t: public platf::high_precision_timer {
  public:
    /**
     * @brief Record each positive delay requested by the frame pacer.
     * @param duration Delay the native timer would have applied.
     */
    void sleep_for(const std::chrono::nanoseconds &duration) override {
      delays.push_back(duration);
    }

    /**
     * @brief Report that this recording timer is always initialized.
     * @return True.
     */
    operator bool() override {
      return true;
    }

    std::vector<std::chrono::nanoseconds> delays;  ///< Delays requested by the frame pacer.
  };
}  // namespace

TEST(VideoSendPacingTest, WaitsForFirstBatchAndEveryCompletePacketGroup) {
  using namespace std::chrono_literals;
  const auto frame_start = std::chrono::steady_clock::now() + 1h;
  stream::video_send_pacing_t pacing {10, frame_start};
  recording_video_timer_t timer;
  ASSERT_TRUE(static_cast<bool>(timer));

  pacing.wait(timer);
  ASSERT_EQ(timer.delays.size(), 1);
  EXPECT_GT(timer.delays.front(), 0ns);
  EXPECT_LE(timer.delays.front(), 1h);
  pacing.record_batch(5);
  pacing.wait(timer);
  EXPECT_EQ(timer.delays.size(), 1);
  pacing.record_batch(5);
  pacing.wait(timer);
  ASSERT_EQ(timer.delays.size(), 2);
  EXPECT_GT(timer.delays.back(), 0ns);
  EXPECT_EQ(pacing.next_frame_start(), frame_start + 1ms);
  pacing.record_batch(2);
  pacing.wait(timer);
  EXPECT_EQ(timer.delays.size(), 2);
  EXPECT_EQ(pacing.next_frame_start(), frame_start + 1200us);
}

TEST(VideoSendPacingTest, KeepsPacketBudgetAcrossFecBlocksAndAllowsPastDeadlines) {
  using namespace std::chrono_literals;
  const auto frame_start = std::chrono::steady_clock::now() - 1h;
  stream::video_send_pacing_t pacing {10, frame_start};
  recording_video_timer_t timer;

  pacing.wait(timer);
  EXPECT_TRUE(timer.delays.empty());
  pacing.record_batch(6);
  pacing.record_batch(9);
  pacing.wait(timer);
  EXPECT_TRUE(timer.delays.empty());
  EXPECT_EQ(pacing.next_frame_start(), frame_start + 1500us);
  pacing.record_batch(5);
  pacing.wait(timer);
  EXPECT_TRUE(timer.delays.empty());
  EXPECT_EQ(pacing.next_frame_start(), frame_start + 2ms);
}

TEST(ConcatAndInsertTests, ConcatNoInsertionTest) {
  std::array<char, 2> b1 = {'a', 'b'};
  std::array<char, 3> b2 = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(0, 2, std::string_view {b1.data(), b1.size()}, std::string_view {b2.data(), b2.size()});
  auto expected = std::vector<uint8_t> {'a', 'b', 'c', 'd', 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatLargeStrideTest) {
  std::array<char, 2> b1 = {'a', 'b'};
  std::array<char, 3> b2 = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(1, b1.size() + b2.size() + 1, std::string_view {b1.data(), b1.size()}, std::string_view {b2.data(), b2.size()});
  auto expected = std::vector<uint8_t> {0, 'a', 'b', 'c', 'd', 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatSmallStrideTest) {
  std::array<char, 2> b1 = {'a', 'b'};
  std::array<char, 3> b2 = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(1, 1, std::string_view {b1.data(), b1.size()}, std::string_view {b2.data(), b2.size()});
  auto expected = std::vector<uint8_t> {0, 'a', 0, 'b', 0, 'c', 0, 'd', 0, 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ControlPacketTests, RejectsZeroLengthPacket) {
  net::packet_t packet {enet_packet_create(nullptr, 0, 0)};

  ASSERT_NE(packet, nullptr);
  EXPECT_EQ(packet->data, nullptr);
  EXPECT_EQ(stream::parse_control_packet(*packet), std::nullopt);
}

TEST(ControlPacketTests, RejectsOneBytePacket) {
  const std::uint8_t data {0x06};
  net::packet_t packet {enet_packet_create(&data, sizeof(data), 0)};

  ASSERT_NE(packet, nullptr);
  EXPECT_EQ(stream::parse_control_packet(*packet), std::nullopt);
}

TEST(ControlPacketTests, AcceptsTypeWithoutPayload) {
  const std::array<std::uint8_t, 2> data {0x06, 0x02};
  net::packet_t packet {enet_packet_create(data.data(), data.size(), 0)};

  ASSERT_NE(packet, nullptr);
  auto message = stream::parse_control_packet(*packet);
  ASSERT_TRUE(message);
  EXPECT_EQ(message->first, 0x0206);
  EXPECT_TRUE(message->second.empty());
}

TEST(ControlPacketTests, AcceptsTypeAndPayload) {
  const std::array<std::uint8_t, 5> data {0x06, 0x02, 'a', 'b', 'c'};
  net::packet_t packet {enet_packet_create(data.data(), data.size(), 0)};

  ASSERT_NE(packet, nullptr);
  auto message = stream::parse_control_packet(*packet);
  ASSERT_TRUE(message);
  EXPECT_EQ(message->first, 0x0206);
  EXPECT_EQ(message->second, "abc");
}
