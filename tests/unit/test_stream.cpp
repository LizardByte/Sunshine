/**
 * @file tests/unit/test_stream.cpp
 * @brief Test src/stream.*
 */

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <src/config.h>
#include <src/rtsp.h>
#include <src/stream.h>
#include <string>
#include <string_view>
#include <vector>

namespace stream {
  std::vector<uint8_t> concat_and_insert(uint64_t insert_size, uint64_t slice_size, const std::string_view &data1, const std::string_view &data2);
}

#include "../tests_common.h"

namespace {
  using namespace std::chrono_literals;

  /** Moonlight feature bit that advertises SS_FRAME_FEC_STATUS support. */
  constexpr int fec_status_feature = 0x01;

  /** Fixed-size Moonlight control payload used by stream integration tests. */
  using frame_fec_status_payload_t =
    std::array<char, stream::network_metrics::frame_fec_status_payload_size>;

  /**
   * @brief Restore global video settings changed by one stream integration test.
   */
  class video_config_guard_t {
  public:
    /**
     * @brief Save the global adaptive bitrate settings.
     */
    video_config_guard_t() = default;

    video_config_guard_t(const video_config_guard_t &) = delete;
    video_config_guard_t &operator=(const video_config_guard_t &) = delete;
    video_config_guard_t(video_config_guard_t &&) = delete;
    video_config_guard_t &operator=(video_config_guard_t &&) = delete;

    /**
     * @brief Restore the saved global adaptive bitrate settings.
     */
    ~video_config_guard_t() {
      ::config::video.adaptive_bitrate = adaptive_bitrate_;
      ::config::video.max_bitrate = max_bitrate_;
    }

  private:
    bool adaptive_bitrate_ = ::config::video.adaptive_bitrate;  ///< Saved adaptive bitrate switch.
    int max_bitrate_ = ::config::video.max_bitrate;  ///< Saved host bitrate ceiling.
  };

  /**
   * @brief Write a big-endian 16-bit integer into a test payload.
   *
   * @param payload Destination payload.
   * @param offset Byte offset to write.
   * @param value Host-endian value.
   */
  void put_u16_be(
    frame_fec_status_payload_t &payload,
    const std::size_t offset,
    const std::uint16_t value
  ) {
    payload[offset] = static_cast<char>(value >> 8);
    payload[offset + 1] = static_cast<char>(value);
  }

  /**
   * @brief Build a valid current-version FEC status report.
   *
   * @return Big-endian SS_FRAME_FEC_STATUS payload with one recoverable loss.
   */
  frame_fec_status_payload_t make_fec_payload() {
    frame_fec_status_payload_t payload {};
    payload[3] = 42;
    put_u16_be(payload, 4, 120);
    put_u16_be(payload, 6, 115);
    put_u16_be(payload, 8, 2);
    put_u16_be(payload, 10, 10);
    put_u16_be(payload, 12, 3);
    put_u16_be(payload, 14, 8);
    put_u16_be(payload, 16, 2);
    payload[18] = 30;
    payload[19] = 0;
    payload[20] = 1;
    return payload;
  }

  /**
   * @brief Build Moonlight's valid uninitialized report for a skipped FEC block.
   *
   * @return Big-endian SS_FRAME_FEC_STATUS payload with zero shard counters.
   */
  frame_fec_status_payload_t make_incomplete_fec_payload() {
    frame_fec_status_payload_t payload {};
    payload[20] = 1;
    return payload;
  }

  /**
   * @brief View a byte array as an immutable control payload.
   *
   * @param payload Byte array to view.
   * @return String view spanning the payload bytes.
   */
  template<std::size_t Size>
  std::string_view payload_view(const std::array<char, Size> &payload) {
    return {payload.data(), payload.size()};
  }

  /**
   * @brief Allocate a stream session with valid launch crypto material.
   *
   * @param config Stream configuration copied into the session.
   * @return Allocated session.
   */
  std::shared_ptr<stream::session_t> make_session(stream::config_t &config) {
    rtsp_stream::launch_session_t launch_session {};
    launch_session.gcm_key.resize(16);
    launch_session.iv.resize(16);
    return stream::session::alloc(config, launch_session);
  }

  /**
   * @brief Allocate a session with adaptive bitrate and FEC telemetry enabled.
   *
   * @return Allocated adaptive bitrate stream session.
   */
  std::shared_ptr<stream::session_t> make_adaptive_session() {
    ::config::video.adaptive_bitrate = true;

    stream::config_t config {};
    config.monitor.bitrate = 25'000;
    config.monitor.framerate = 60;
    config.mlFeatureFlags = fec_status_feature;
    return make_session(config);
  }
}  // namespace

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

TEST(StreamSessionAdaptiveBitrateTests, AppliesHostCeilingIndependentlyOfAdaptiveControl) {
  video_config_guard_t guard;
  ::config::video.max_bitrate = 15'000;

  stream::config_t fixed_config {};
  fixed_config.monitor.bitrate = 25'000;
  fixed_config.monitor.framerate = 60;
  fixed_config.mlFeatureFlags = fec_status_feature;
  ::config::video.adaptive_bitrate = false;
  auto fixed_session = make_session(fixed_config);
  ASSERT_TRUE(fixed_session);
  EXPECT_EQ(15'000U, stream::session::testing::configured_video_bitrate(*fixed_session));
  auto fixed_requests = stream::session::testing::video_bitrate_requests(*fixed_session);
  stream::session::testing::process_adaptive_bitrate(*fixed_session, std::chrono::steady_clock::now(), 20, 2, 1);
  EXPECT_FALSE(fixed_requests->try_pop());

  stream::config_t non_fec_config = fixed_config;
  non_fec_config.mlFeatureFlags = 0;
  ::config::video.adaptive_bitrate = true;
  auto non_fec_session = make_session(non_fec_config);
  ASSERT_TRUE(non_fec_session);
  EXPECT_EQ(15'000U, stream::session::testing::configured_video_bitrate(*non_fec_session));
  auto non_fec_requests = stream::session::testing::video_bitrate_requests(*non_fec_session);
  stream::session::testing::process_adaptive_bitrate(*non_fec_session, std::chrono::steady_clock::now(), 20, 2, 1);
  EXPECT_FALSE(non_fec_requests->try_pop());

  stream::config_t adaptive_config = fixed_config;
  auto adaptive_session = make_session(adaptive_config);
  ASSERT_TRUE(adaptive_session);
  EXPECT_EQ(15'000U, stream::session::testing::configured_video_bitrate(*adaptive_session));
  auto adaptive_requests = stream::session::testing::video_bitrate_requests(*adaptive_session);
  stream::session::testing::process_adaptive_bitrate(*adaptive_session, std::chrono::steady_clock::now(), 20, 2, 1);
  const auto probe = adaptive_requests->try_pop();
  ASSERT_TRUE(probe);
  EXPECT_EQ(15'000U, probe->target_kbps);
}

TEST(StreamSessionAdaptiveBitrateTests, IngestsAndPublishesFecTelemetryThroughControlOrchestration) {
  video_config_guard_t guard;
  auto session = make_adaptive_session();
  ASSERT_TRUE(session);
  const auto now = std::chrono::steady_clock::now();
  const auto payload = make_fec_payload();

  EXPECT_EQ(
    stream::network_metrics::ingest_result_e::accepted,
    stream::session::testing::ingest_frame_fec_status(*session, payload_view(payload), now, 20, 2)
  );
  EXPECT_TRUE(stream::session::testing::publish_network_metrics(*session, now + 500ms, 20, 2));
  EXPECT_TRUE(stream::session::testing::adaptive_bitrate_telemetry_seen(*session));

  stream::session::testing::record_frame_loss_request(*session, now + 501ms, 20, 2);
  EXPECT_TRUE(stream::session::testing::publish_network_metrics(*session, now + 1001ms, 20, 2));
}

TEST(StreamSessionAdaptiveBitrateTests, IncompleteFecReportsTriggerConservativeDecrease) {
  video_config_guard_t guard;
  auto session = make_adaptive_session();
  ASSERT_TRUE(session);
  auto requests = stream::session::testing::video_bitrate_requests(*session);
  const auto now = std::chrono::steady_clock::now();

  stream::session::testing::process_adaptive_bitrate(*session, now, 20, 2, 1);
  const auto capability_check = requests->try_pop();
  ASSERT_TRUE(capability_check);
  stream::session::testing::publish_video_bitrate_result(
    *session,
    {
      video::bitrate_reconfigure_status_e::unchanged,
      25'000,
      capability_check->target_kbps,
      25'000,
    }
  );
  stream::session::testing::process_adaptive_bitrate(*session, now + 1ms, 20, 2, 2);

  const auto incomplete = make_incomplete_fec_payload();
  EXPECT_EQ(
    stream::network_metrics::ingest_result_e::accepted,
    stream::session::testing::ingest_frame_fec_status(*session, payload_view(incomplete), now + 2ms, 20, 2)
  );
  EXPECT_TRUE(stream::session::testing::publish_network_metrics(*session, now + 502ms, 20, 2));
  EXPECT_EQ(
    stream::network_metrics::ingest_result_e::accepted,
    stream::session::testing::ingest_frame_fec_status(*session, payload_view(incomplete), now + 503ms, 20, 2)
  );
  EXPECT_TRUE(stream::session::testing::publish_network_metrics(*session, now + 1003ms, 20, 2));

  stream::session::testing::process_adaptive_bitrate(*session, now + 1003ms, 20, 2, 3);
  const auto decrease = requests->try_pop();
  ASSERT_TRUE(decrease);
  EXPECT_EQ(18'700U, decrease->target_kbps);
}

TEST(StreamSessionAdaptiveBitrateTests, AcknowledgesEncoderFailureAndStopsDispatching) {
  video_config_guard_t guard;
  auto session = make_adaptive_session();
  ASSERT_TRUE(session);
  auto requests = stream::session::testing::video_bitrate_requests(*session);
  const auto now = std::chrono::steady_clock::now();

  stream::session::testing::process_adaptive_bitrate(*session, now, 20, 2, 1);
  const auto probe = requests->try_pop();
  ASSERT_TRUE(probe);
  stream::session::testing::publish_video_bitrate_result(
    *session,
    {
      video::bitrate_reconfigure_status_e::unsupported,
      25'000,
      probe->target_kbps,
      25'000,
    }
  );
  stream::session::testing::process_adaptive_bitrate(*session, now + 100ms, 20, 2, 2);

  EXPECT_TRUE(stream::session::testing::adaptive_bitrate_in_fallback(*session));
  EXPECT_FALSE(requests->try_pop());
}
