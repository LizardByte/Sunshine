/**
 * @file src/network_metrics.h
 * @brief Declarations for per-session Moonlight network telemetry.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace stream::network_metrics {
  /** Aggregation period for client network telemetry. */
  inline constexpr std::chrono::milliseconds aggregation_window {500};

  /** Size of the current SS_FRAME_FEC_STATUS wire payload. */
  inline constexpr std::size_t frame_fec_status_payload_size = 21;

  /** Default and minimum number of FEC reports processed per session window. */
  inline constexpr std::uint32_t default_report_limit_per_window = 512;

  /** Protocol maximum number of FEC blocks emitted for one video frame. */
  inline constexpr std::uint32_t max_fec_blocks_per_frame = 4;

  /** Hard upper bound for a configured per-session report limit. */
  inline constexpr std::uint32_t hard_max_reports_per_window = 4096;

  /** Monotonic time point used to delimit aggregation windows. */
  using time_point_t = std::chrono::steady_clock::time_point;

  /**
   * @brief Describes how an incoming FEC report was handled.
   */
  enum class ingest_result_e {
    accepted,  ///< The payload was valid and added to the current window.
    malformed,  ///< The payload size or field relationships were invalid.
    protocol_mismatch,  ///< The client did not advertise the current FEC status feature.
    rate_limited,  ///< The per-window processing limit was already reached.
  };

  /**
   * @brief Host-endian representation of one SS_FRAME_FEC_STATUS message.
   */
  struct frame_fec_status_t {
    std::uint32_t frame_index;  ///< Client frame index associated with the report.
    std::uint16_t highest_received_sequence_number;  ///< Highest RTP sequence number observed by the client.
    std::uint16_t next_contiguous_sequence_number;  ///< Client contiguous-prefix cursor when fast-path tracking stopped.
    std::uint16_t missing_packets_before_highest_received;  ///< Data or parity gaps observed before the highest received packet.
    std::uint16_t total_data_packets;  ///< Data shards expected for the FEC block.
    std::uint16_t total_parity_packets;  ///< Parity shards expected for the FEC block.
    std::uint16_t received_data_packets;  ///< Data shards received from the network.
    std::uint16_t received_parity_packets;  ///< Parity shards received from the network.
    std::uint8_t fec_percentage;  ///< FEC percentage encoded by Sunshine for this FEC block.
    std::uint8_t multi_fec_block_index;  ///< Zero-based FEC block index within the frame.
    std::uint8_t multi_fec_block_count;  ///< Total FEC block count for the frame.
  };

  /**
   * @brief Immutable aggregate published for one telemetry window.
   *
   * @note Moonlight emits FEC status reports only around recovery and drop
   * events, over a bounded best-effort queue. An empty window therefore means
   * "no report observed", not "the network had no packet loss".
   */
  struct snapshot_t {
    std::uint64_t sequence;  ///< Per-session snapshot sequence number.
    std::uint64_t window_duration_ms;  ///< Actual monotonic window duration in milliseconds.
    std::uint64_t fec_reports;  ///< Valid FEC reports accepted in the window.
    std::uint64_t incomplete_fec_reports;  ///< Reports emitted for a missing FEC block before counters were initialized.
    std::uint64_t missing_packets;  ///< Client-reported gaps before the highest received packets.
    std::uint64_t unrecovered_data_packets;  ///< Missing data shards from blocks that could not be decoded.
    std::uint64_t received_data_packets;  ///< Data shards received across all accepted reports.
    std::uint64_t received_parity_packets;  ///< Parity shards received across all accepted reports.
    std::uint64_t fec_recovered_data_packets;  ///< Missing data shards reconstructed by decodable FEC blocks.
    std::uint64_t frame_loss_requests;  ///< IDR or reference-frame invalidation requests received in the window.
    std::uint64_t malformed_reports;  ///< Reports rejected because their payload was invalid.
    std::uint64_t protocol_mismatch_reports;  ///< Reports received without advertised FEC status support.
    std::uint64_t rate_limited_reports;  ///< Reports skipped after the processing limit was reached.
    std::uint32_t rtt_ms;  ///< ENet smoothed round-trip time at publication.
    std::uint32_t rtt_variance_ms;  ///< ENet round-trip-time variance at publication.
  };

  /**
   * @brief Decode and validate a current-version SS_FRAME_FEC_STATUS payload.
   *
   * @param payload Raw 21-byte big-endian payload received on the control channel.
   * @return Decoded host-endian fields, or no value when the payload is invalid.
   */
  std::optional<frame_fec_status_t> parse_frame_fec_status(std::string_view payload);

  /**
   * @brief Derive a bounded telemetry report limit from the requested frame rate.
   *
   * The estimate allows four FEC blocks per frame during the 500 ms aggregation
   * window, plus 25 percent headroom for reports arriving in short bursts. The
   * result is rounded up, never falls below default_report_limit_per_window,
   * and never exceeds hard_max_reports_per_window.
   *
   * @param frames_per_second Requested stream frame rate.
   * @return Safe per-session FEC report limit for one aggregation window.
   */
  [[nodiscard]] std::uint32_t report_limit_for_framerate(std::uint32_t frames_per_second);

  /**
   * @brief Aggregates Moonlight FEC reports for one stream session.
   *
   * The tracker is owned by the control thread. Call poll() before ingest() when
   * processing a new event so an elapsed window is published before that event
   * starts the next window. Reports may be lost, reordered, or repeated for
   * different FEC blocks of the same frame, so aggregation never infers a
   * complete per-frame timeline.
   */
  class tracker_t {
  public:
    /**
     * @brief Create a tracker whose first window starts at the supplied time.
     *
     * @param window_start Start of the first aggregation window.
     * @param report_limit Maximum reports admitted in each aggregation window.
     * Values outside the supported range are clamped.
     */
    explicit tracker_t(
      time_point_t window_start = std::chrono::steady_clock::now(),
      std::uint32_t report_limit = default_report_limit_per_window
    );

    /**
     * @brief Validate and add one client FEC report to the current window.
     *
     * @param payload Raw SS_FRAME_FEC_STATUS payload.
     * @param feature_advertised Whether the client advertised ML_FF_FEC_STATUS for the session.
     * @return Result describing whether the report was accepted or rejected.
     */
    ingest_result_e ingest(std::string_view payload, bool feature_advertised);

    /**
     * @brief Count one client frame-loss recovery request in the current window.
     */
    void record_frame_loss_request();

    /**
     * @brief Publish and reset an elapsed non-empty aggregation window.
     *
     * @param now Monotonic publication time.
     * @param rtt_ms Current ENet smoothed round-trip time.
     * @param rtt_variance_ms Current ENet round-trip-time variance.
     * @return Completed snapshot, or no value before expiry or for an empty window.
     */
    std::optional<snapshot_t> poll(time_point_t now, std::uint32_t rtt_ms, std::uint32_t rtt_variance_ms);

  private:
    /**
     * @brief Mutable counters for the active aggregation window.
     */
    struct accumulator_t {
      std::uint32_t processed_reports = 0;  ///< Reports admitted to validation in this window.
      std::uint64_t fec_reports = 0;  ///< Valid FEC reports.
      std::uint64_t incomplete_fec_reports = 0;  ///< Reports for a skipped block with uninitialized counters.
      std::uint64_t missing_packets = 0;  ///< Client-reported packet gaps.
      std::uint64_t unrecovered_data_packets = 0;  ///< Data shards that remained unrecovered.
      std::uint64_t received_data_packets = 0;  ///< Data shards received.
      std::uint64_t received_parity_packets = 0;  ///< Parity shards received.
      std::uint64_t fec_recovered_data_packets = 0;  ///< Data shards recovered through FEC.
      std::uint64_t frame_loss_requests = 0;  ///< IDR or reference-frame invalidation requests.
      std::uint64_t malformed_reports = 0;  ///< Invalid payloads.
      std::uint64_t protocol_mismatch_reports = 0;  ///< Reports without advertised support.
      std::uint64_t rate_limited_reports = 0;  ///< Reports skipped by the processing limit.

      /**
       * @brief Return whether this window contains any report or frame-loss activity.
       *
       * @return `true` when publishing the window would expose useful telemetry.
       */
      bool has_activity() const;
    };

    time_point_t window_start_;  ///< Monotonic start of the active window.
    std::uint32_t report_limit_;  ///< Maximum reports admitted in one window.
    std::uint64_t next_sequence_ = 1;  ///< Sequence assigned to the next published snapshot.
    accumulator_t accumulator_;  ///< Counters for the active window.
  };
}  // namespace stream::network_metrics
