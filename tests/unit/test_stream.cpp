/**
 * @file tests/unit/test_stream.cpp
 * @brief Test src/stream.*
 */

#include <cstdint>
#include <functional>
#include <src/config.h>
#include <src/rtsp.h>
#include <src/stream.h>
#include <string>
#include <thread>
#include <vector>

namespace stream {
  std::vector<uint8_t> concat_and_insert(uint64_t insert_size, uint64_t slice_size, const std::string_view &data1, const std::string_view &data2);
}

#include "../tests_common.h"

TEST(ConcatAndInsertTests, ConcatNoInsertionTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(0, 2, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {'a', 'b', 'c', 'd', 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatLargeStrideTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(1, sizeof(b1) + sizeof(b2) + 1, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {0, 'a', 'b', 'c', 'd', 'e'};
  ASSERT_EQ(res, expected);
}

TEST(ConcatAndInsertTests, ConcatSmallStrideTest) {
  char b1[] = {'a', 'b'};
  char b2[] = {'c', 'd', 'e'};
  auto res = stream::concat_and_insert(1, 1, std::string_view {b1, sizeof(b1)}, std::string_view {b2, sizeof(b2)});
  auto expected = std::vector<uint8_t> {0, 'a', 0, 'b', 0, 'c', 0, 'd', 0, 'e'};
  ASSERT_EQ(res, expected);
}

TEST(StreamSessionBitrateRequestTests, PublishesRequestAcrossThreads) {
  stream::config_t config {};
  rtsp_stream::launch_session_t launch_session {};
  launch_session.gcm_key.resize(16);
  launch_session.iv.resize(16);
  auto session = stream::session::alloc(config, launch_session);
  auto requests = stream::session::testing::video_bitrate_requests(*session);

  std::jthread producer {[&session] {
    stream::session::request_video_bitrate(*session, 15'000, "network-window");
  }};
  producer.join();

  const auto request = requests->try_pop();
  ASSERT_TRUE(request);
  EXPECT_EQ(15'000U, request->target_kbps);
  EXPECT_EQ("network-window", request->reason);
}

TEST(StreamSessionBitrateRequestTests, RetainsOnlyLatestPendingRequest) {
  stream::config_t config {};
  rtsp_stream::launch_session_t launch_session {};
  launch_session.gcm_key.resize(16);
  launch_session.iv.resize(16);
  auto session = stream::session::alloc(config, launch_session);
  auto requests = stream::session::testing::video_bitrate_requests(*session);

  stream::session::request_video_bitrate(*session, 25'000, "initial");
  stream::session::request_video_bitrate(*session, 40'000, "latest");

  const auto request = requests->try_pop();
  ASSERT_TRUE(request);
  EXPECT_EQ(40'000U, request->target_kbps);
  EXPECT_EQ("latest", request->reason);
  EXPECT_FALSE(requests->try_pop());
}

TEST(StreamSessionBitrateRequestTests, AppliesHostCeilingToNativeEncoderSessionConfig) {
  const auto saved_max_bitrate = ::config::video.max_bitrate;
  ::config::video.max_bitrate = 15'000;

  stream::config_t stream_config {};
  stream_config.monitor.bitrate = 25'000;
  rtsp_stream::launch_session_t launch_session {};
  launch_session.gcm_key.resize(16);
  launch_session.iv.resize(16);
  auto session = stream::session::alloc(stream_config, launch_session);

  ::config::video.max_bitrate = saved_max_bitrate;
  ASSERT_TRUE(session);
  EXPECT_EQ(stream::session::testing::configured_video_bitrate(*session), 15'000U);
}
