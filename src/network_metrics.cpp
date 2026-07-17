/**
 * @file src/network_metrics.cpp
 * @brief Definitions for per-session Moonlight network telemetry.
 */

// local includes
#include "network_metrics.h"

namespace stream::network_metrics {
  namespace {
    /**
     * @brief Read a big-endian 16-bit integer from a validated payload.
     *
     * @param payload Raw protocol payload.
     * @param offset Byte offset to read.
     * @return Host-endian integer.
     */
    std::uint16_t read_u16_be(const std::string_view payload, const std::size_t offset) {
      const auto *bytes = reinterpret_cast<const std::uint8_t *>(payload.data());
      return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8) | bytes[offset + 1]);
    }

    /**
     * @brief Read a big-endian 32-bit integer from a validated payload.
     *
     * @param payload Raw protocol payload.
     * @param offset Byte offset to read.
     * @return Host-endian integer.
     */
    std::uint32_t read_u32_be(const std::string_view payload, const std::size_t offset) {
      const auto *bytes = reinterpret_cast<const std::uint8_t *>(payload.data());
      return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
             bytes[offset + 3];
    }

    /**
     * @brief Check invariants guaranteed by Moonlight's FEC queue implementation.
     *
     * @param status Decoded status to validate.
     * @return `true` when all count and block relationships are valid.
     */
    bool has_valid_relationships(const frame_fec_status_t &status) {
      constexpr std::uint8_t max_multi_fec_blocks = 4;
      const auto total_packets = static_cast<std::uint32_t>(status.total_data_packets) + status.total_parity_packets;
      const auto valid_missing_count = total_packets == 0 ?
                                         status.missing_packets_before_highest_received == 0 :
                                         status.missing_packets_before_highest_received < total_packets;
      const auto valid_counts = status.received_data_packets <= status.total_data_packets &&
                                status.received_parity_packets <= status.total_parity_packets &&
                                valid_missing_count;
      const auto valid_blocks = status.multi_fec_block_count != 0 &&
                                status.multi_fec_block_count <= max_multi_fec_blocks &&
                                status.multi_fec_block_index < status.multi_fec_block_count;
      return valid_counts && valid_blocks;
    }
  }  // namespace

  std::optional<frame_fec_status_t> parse_frame_fec_status(const std::string_view payload) {
    if (payload.size() != frame_fec_status_payload_size) {
      return std::nullopt;
    }

    const auto *bytes = reinterpret_cast<const std::uint8_t *>(payload.data());

    frame_fec_status_t status {
      .frame_index = read_u32_be(payload, 0),
      .highest_received_sequence_number = read_u16_be(payload, 4),
      .next_contiguous_sequence_number = read_u16_be(payload, 6),
      .missing_packets_before_highest_received = read_u16_be(payload, 8),
      .total_data_packets = read_u16_be(payload, 10),
      .total_parity_packets = read_u16_be(payload, 12),
      .received_data_packets = read_u16_be(payload, 14),
      .received_parity_packets = read_u16_be(payload, 16),
      .fec_percentage = bytes[18],
      .multi_fec_block_index = bytes[19],
      .multi_fec_block_count = bytes[20],
    };

    return status;
  }

  tracker_t::tracker_t(const time_point_t window_start):
      window_start_ {window_start} {
  }

  bool tracker_t::accumulator_t::has_activity() const {
    return processed_reports != 0 || idr_requests != 0;
  }

  ingest_result_e tracker_t::ingest(const std::string_view payload, const bool feature_advertised) {
    if (accumulator_.processed_reports >= max_reports_per_window) {
      ++accumulator_.rate_limited_reports;
      return ingest_result_e::rate_limited;
    }
    ++accumulator_.processed_reports;

    if (!feature_advertised) {
      ++accumulator_.protocol_mismatch_reports;
      return ingest_result_e::protocol_mismatch;
    }

    auto status = parse_frame_fec_status(payload);
    if (!status || !has_valid_relationships(*status)) {
      ++accumulator_.malformed_reports;
      return ingest_result_e::malformed;
    }

    ++accumulator_.fec_reports;
    accumulator_.missing_packets += status->missing_packets_before_highest_received;
    accumulator_.received_data_packets += status->received_data_packets;
    accumulator_.received_parity_packets += status->received_parity_packets;

    const auto missing_data_packets = status->total_data_packets > status->received_data_packets ?
                                        static_cast<std::uint64_t>(status->total_data_packets - status->received_data_packets) :
                                        0;
    const auto received_shards = static_cast<std::uint32_t>(status->received_data_packets) + status->received_parity_packets;
    if (received_shards >= status->total_data_packets) {
      accumulator_.fec_recovered_data_packets += missing_data_packets;
    } else {
      accumulator_.unrecovered_data_packets += missing_data_packets;
    }

    return ingest_result_e::accepted;
  }

  void tracker_t::record_idr_request() {
    ++accumulator_.idr_requests;
  }

  std::optional<snapshot_t> tracker_t::poll(
    const time_point_t now,
    const std::uint32_t rtt_ms,
    const std::uint32_t rtt_variance_ms
  ) {
    const auto elapsed = now - window_start_;
    if (elapsed < aggregation_window) {
      return std::nullopt;
    }

    const auto had_activity = accumulator_.has_activity();
    snapshot_t snapshot {
      .sequence = next_sequence_,
      .window_duration_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()),
      .fec_reports = accumulator_.fec_reports,
      .missing_packets = accumulator_.missing_packets,
      .unrecovered_data_packets = accumulator_.unrecovered_data_packets,
      .received_data_packets = accumulator_.received_data_packets,
      .received_parity_packets = accumulator_.received_parity_packets,
      .fec_recovered_data_packets = accumulator_.fec_recovered_data_packets,
      .idr_requests = accumulator_.idr_requests,
      .malformed_reports = accumulator_.malformed_reports,
      .protocol_mismatch_reports = accumulator_.protocol_mismatch_reports,
      .rate_limited_reports = accumulator_.rate_limited_reports,
      .rtt_ms = rtt_ms,
      .rtt_variance_ms = rtt_variance_ms,
    };

    window_start_ = now;
    accumulator_ = {};
    if (!had_activity) {
      return std::nullopt;
    }

    ++next_sequence_;
    return snapshot;
  }
}  // namespace stream::network_metrics
