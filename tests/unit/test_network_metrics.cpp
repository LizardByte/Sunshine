/**
 * @file tests/unit/test_network_metrics.cpp
 * @brief Test src/network_metrics.*.
 */

// standard includes
#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

// local includes
#include "../tests_common.h"

#include <src/network_metrics.h>

namespace {
  using namespace std::chrono_literals;
  using stream::network_metrics::frame_fec_status_payload_size;

  /**
   * @brief Write a big-endian 16-bit integer into a test payload.
   *
   * @param payload Destination payload.
   * @param offset Byte offset to write.
   * @param value Host-endian value.
   */
  void put_u16_be(std::array<std::uint8_t, frame_fec_status_payload_size> &payload, const std::size_t offset, const std::uint16_t value) {
    payload[offset] = static_cast<std::uint8_t>(value >> 8);
    payload[offset + 1] = static_cast<std::uint8_t>(value);
  }

  /**
   * @brief Write a big-endian 32-bit integer into a test payload.
   *
   * @param payload Destination payload.
   * @param offset Byte offset to write.
   * @param value Host-endian value.
   */
  void put_u32_be(std::array<std::uint8_t, frame_fec_status_payload_size> &payload, const std::size_t offset, const std::uint32_t value) {
    payload[offset] = static_cast<std::uint8_t>(value >> 24);
    payload[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    payload[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    payload[offset + 3] = static_cast<std::uint8_t>(value);
  }

  /**
   * @brief Build a valid current-version FEC report for tracker tests.
   *
   * @param total_data Expected data-shard count.
   * @param total_parity Expected parity-shard count.
   * @param received_data Received data-shard count.
   * @param received_parity Received parity-shard count.
   * @param missing Client-reported packet gaps.
   * @return Big-endian SS_FRAME_FEC_STATUS payload.
   */
  std::array<std::uint8_t, frame_fec_status_payload_size> make_payload(
    const std::uint16_t total_data,
    const std::uint16_t total_parity,
    const std::uint16_t received_data,
    const std::uint16_t received_parity,
    const std::uint16_t missing
  ) {
    std::array<std::uint8_t, frame_fec_status_payload_size> payload {};
    put_u32_be(payload, 0, 42);
    put_u16_be(payload, 4, 120);
    put_u16_be(payload, 6, 115);
    put_u16_be(payload, 8, missing);
    put_u16_be(payload, 10, total_data);
    put_u16_be(payload, 12, total_parity);
    put_u16_be(payload, 14, received_data);
    put_u16_be(payload, 16, received_parity);
    payload[18] = 20;
    payload[19] = 0;
    payload[20] = 1;
    return payload;
  }

  /**
   * @brief View a byte array as an immutable protocol payload.
   *
   * @param payload Byte array to view.
   * @return String view spanning the payload bytes.
   */
  template<std::size_t Size>
  std::string_view payload_view(const std::array<std::uint8_t, Size> &payload) {
    return {reinterpret_cast<const char *>(payload.data()), payload.size()};
  }
}  // namespace

TEST(FrameFecStatusTests, ParsesCurrentBigEndianWireFormat) {
  std::array<std::uint8_t, frame_fec_status_payload_size> payload {};
  for (std::size_t index = 0; index < payload.size(); ++index) {
    payload[index] = static_cast<std::uint8_t>(index + 1);
  }

  auto status = stream::network_metrics::parse_frame_fec_status(payload_view(payload));

  ASSERT_TRUE(status);
  EXPECT_EQ(status->frame_index, 0x01020304u);
  EXPECT_EQ(status->highest_received_sequence_number, 0x0506u);
  EXPECT_EQ(status->next_contiguous_sequence_number, 0x0708u);
  EXPECT_EQ(status->missing_packets_before_highest_received, 0x090Au);
  EXPECT_EQ(status->total_data_packets, 0x0B0Cu);
  EXPECT_EQ(status->total_parity_packets, 0x0D0Eu);
  EXPECT_EQ(status->received_data_packets, 0x0F10u);
  EXPECT_EQ(status->received_parity_packets, 0x1112u);
  EXPECT_EQ(status->fec_percentage, 0x13u);
  EXPECT_EQ(status->multi_fec_block_index, 0x14u);
  EXPECT_EQ(status->multi_fec_block_count, 0x15u);
}

TEST(FrameFecStatusTests, RejectsPayloadWithWrongVersionSize) {
  std::array<std::uint8_t, frame_fec_status_payload_size - 1> short_payload {};
  std::array<std::uint8_t, frame_fec_status_payload_size + 1> long_payload {};

  EXPECT_FALSE(stream::network_metrics::parse_frame_fec_status(payload_view(short_payload)));
  EXPECT_FALSE(stream::network_metrics::parse_frame_fec_status(payload_view(long_payload)));
}

TEST(NetworkMetricsTrackerTests, RequiresAdvertisedFeature) {
  const auto start = stream::network_metrics::time_point_t {};
  stream::network_metrics::tracker_t tracker {start};
  auto payload = make_payload(10, 3, 8, 2, 2);

  EXPECT_EQ(
    tracker.ingest(payload_view(payload), false),
    stream::network_metrics::ingest_result_e::protocol_mismatch
  );

  auto snapshot = tracker.poll(start + 500ms, 7, 2);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->fec_reports, 0u);
  EXPECT_EQ(snapshot->protocol_mismatch_reports, 1u);
}

TEST(NetworkMetricsTrackerTests, CountsMalformedPayloads) {
  const auto start = stream::network_metrics::time_point_t {};
  stream::network_metrics::tracker_t tracker {start};
  std::array<std::uint8_t, frame_fec_status_payload_size - 1> payload {};

  EXPECT_EQ(
    tracker.ingest(payload_view(payload), true),
    stream::network_metrics::ingest_result_e::malformed
  );

  auto snapshot = tracker.poll(start + 500ms, 7, 2);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->malformed_reports, 1u);
}

TEST(NetworkMetricsTrackerTests, RejectsInvalidFieldRelationships) {
  const auto start = stream::network_metrics::time_point_t {};
  stream::network_metrics::tracker_t tracker {start};
  auto excessive_data = make_payload(10, 3, 11, 2, 0);
  auto excessive_parity = make_payload(10, 3, 8, 4, 0);
  auto excessive_missing = make_payload(10, 3, 8, 2, 13);
  auto zero_block_count = make_payload(10, 3, 8, 2, 0);
  auto excessive_block_count = make_payload(10, 3, 8, 2, 0);
  auto invalid_block_index = make_payload(10, 3, 8, 2, 0);
  zero_block_count[20] = 0;
  excessive_block_count[20] = 5;
  invalid_block_index[19] = 1;

  EXPECT_EQ(tracker.ingest(payload_view(excessive_data), true), stream::network_metrics::ingest_result_e::malformed);
  EXPECT_EQ(tracker.ingest(payload_view(excessive_parity), true), stream::network_metrics::ingest_result_e::malformed);
  EXPECT_EQ(tracker.ingest(payload_view(excessive_missing), true), stream::network_metrics::ingest_result_e::malformed);
  EXPECT_EQ(tracker.ingest(payload_view(zero_block_count), true), stream::network_metrics::ingest_result_e::malformed);
  EXPECT_EQ(tracker.ingest(payload_view(excessive_block_count), true), stream::network_metrics::ingest_result_e::malformed);
  EXPECT_EQ(tracker.ingest(payload_view(invalid_block_index), true), stream::network_metrics::ingest_result_e::malformed);

  auto snapshot = tracker.poll(start + 500ms, 7, 2);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->fec_reports, 0u);
  EXPECT_EQ(snapshot->malformed_reports, 6u);
}

TEST(NetworkMetricsTrackerTests, HandlesZeroTotalIncompleteReports) {
  const auto start = stream::network_metrics::time_point_t {};
  stream::network_metrics::tracker_t tracker {start};
  auto incomplete = make_payload(0, 0, 0, 0, 0);
  auto invalid_missing = make_payload(0, 0, 0, 0, 1);

  EXPECT_EQ(tracker.ingest(payload_view(incomplete), true), stream::network_metrics::ingest_result_e::accepted);
  EXPECT_EQ(tracker.ingest(payload_view(invalid_missing), true), stream::network_metrics::ingest_result_e::malformed);

  auto snapshot = tracker.poll(start + 500ms, 7, 2);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->fec_reports, 1u);
  EXPECT_EQ(snapshot->malformed_reports, 1u);
  EXPECT_EQ(snapshot->received_data_packets, 0u);
  EXPECT_EQ(snapshot->received_parity_packets, 0u);
}

TEST(NetworkMetricsTrackerTests, AggregatesRecoveredAndUnrecoveredBlocks) {
  const auto start = stream::network_metrics::time_point_t {};
  stream::network_metrics::tracker_t tracker {start};
  auto recovered = make_payload(10, 3, 8, 2, 2);
  auto unrecovered = make_payload(10, 3, 7, 2, 3);

  EXPECT_EQ(tracker.ingest(payload_view(recovered), true), stream::network_metrics::ingest_result_e::accepted);
  EXPECT_EQ(tracker.ingest(payload_view(unrecovered), true), stream::network_metrics::ingest_result_e::accepted);
  tracker.record_idr_request();
  tracker.record_idr_request();

  EXPECT_FALSE(tracker.poll(start + 499ms, 9, 3));
  auto snapshot = tracker.poll(start + 500ms, 9, 3);

  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->sequence, 1u);
  EXPECT_EQ(snapshot->window_duration_ms, 500u);
  EXPECT_EQ(snapshot->fec_reports, 2u);
  EXPECT_EQ(snapshot->missing_packets, 5u);
  EXPECT_EQ(snapshot->unrecovered_data_packets, 3u);
  EXPECT_EQ(snapshot->received_data_packets, 15u);
  EXPECT_EQ(snapshot->received_parity_packets, 4u);
  EXPECT_EQ(snapshot->fec_recovered_data_packets, 2u);
  EXPECT_EQ(snapshot->idr_requests, 2u);
  EXPECT_EQ(snapshot->rtt_ms, 9u);
  EXPECT_EQ(snapshot->rtt_variance_ms, 3u);
}

TEST(NetworkMetricsTrackerTests, KeepsSessionsIsolated) {
  const auto start = stream::network_metrics::time_point_t {};
  stream::network_metrics::tracker_t first {start};
  stream::network_metrics::tracker_t second {start};
  auto first_payload = make_payload(10, 3, 8, 2, 2);
  auto second_payload = make_payload(20, 4, 20, 0, 0);

  first.ingest(payload_view(first_payload), true);
  second.ingest(payload_view(second_payload), true);
  second.record_idr_request();

  auto first_snapshot = first.poll(start + 500ms, 5, 1);
  auto second_snapshot = second.poll(start + 500ms, 15, 4);

  ASSERT_TRUE(first_snapshot);
  ASSERT_TRUE(second_snapshot);
  EXPECT_EQ(first_snapshot->received_data_packets, 8u);
  EXPECT_EQ(first_snapshot->fec_recovered_data_packets, 2u);
  EXPECT_EQ(first_snapshot->idr_requests, 0u);
  EXPECT_EQ(first_snapshot->rtt_ms, 5u);
  EXPECT_EQ(second_snapshot->received_data_packets, 20u);
  EXPECT_EQ(second_snapshot->fec_recovered_data_packets, 0u);
  EXPECT_EQ(second_snapshot->idr_requests, 1u);
  EXPECT_EQ(second_snapshot->rtt_ms, 15u);
}

TEST(NetworkMetricsTrackerTests, RateLimitsOneWindow) {
  const auto start = stream::network_metrics::time_point_t {};
  stream::network_metrics::tracker_t tracker {start};
  auto payload = make_payload(10, 3, 8, 2, 2);

  for (std::uint32_t report = 0; report < stream::network_metrics::max_reports_per_window; ++report) {
    EXPECT_EQ(tracker.ingest(payload_view(payload), true), stream::network_metrics::ingest_result_e::accepted);
  }
  for (std::uint32_t report = 0; report < 3; ++report) {
    EXPECT_EQ(tracker.ingest(payload_view(payload), true), stream::network_metrics::ingest_result_e::rate_limited);
  }

  auto snapshot = tracker.poll(start + 500ms, 8, 2);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->fec_reports, stream::network_metrics::max_reports_per_window);
  EXPECT_EQ(snapshot->rate_limited_reports, 3u);
}

TEST(NetworkMetricsTrackerTests, DoesNotPublishEmptyWindows) {
  const auto start = stream::network_metrics::time_point_t {};
  stream::network_metrics::tracker_t tracker {start};

  EXPECT_FALSE(tracker.poll(start + 500ms, 8, 2));
  tracker.record_idr_request();
  auto snapshot = tracker.poll(start + 1000ms, 8, 2);

  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->sequence, 1u);
  EXPECT_EQ(snapshot->fec_reports, 0u);
  EXPECT_EQ(snapshot->idr_requests, 1u);
}
