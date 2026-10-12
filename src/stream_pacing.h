/**
 * @file src/stream_pacing.h
 * @brief Packet-batch pacing for one encoded video frame.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstddef>

// local includes
#include "platform/common.h"

namespace stream {
  /**
   * @brief Pace batches across every FEC block belonging to the same video frame.
   */
  class video_send_pacing_t {
  public:
    /**
     * @brief Begin pacing a frame after the preceding frame's final packet group.
     * @param packets_per_millisecond Nonzero maximum packets to send in each pacing group.
     * @param frame_start Earliest transmission time for this frame.
     */
    video_send_pacing_t(std::size_t packets_per_millisecond, std::chrono::steady_clock::time_point frame_start):
        packets_per_millisecond_ {packets_per_millisecond},
        frame_start_ {frame_start} {}

    /**
     * @brief Wait before the first batch or after sending a complete pacing group.
     * @param timer Native timer used to wait until the next packet deadline.
     */
    void wait(platf::high_precision_timer &timer) {
      if (group_packets_sent_ >= packets_per_millisecond_ || frame_packets_sent_ == 0) {
        const auto due = next_frame_start();
        const auto now = std::chrono::steady_clock::now();
        if (now < due) {
          timer.sleep_for(due - now);
        }
        group_packets_sent_ = 0;
      }
    }

    /**
     * @brief Record a completed batch, including individually sent fallback packets.
     * @param count Number of packets in the completed batch.
     */
    void record_batch(std::size_t count) {
      group_packets_sent_ += count;
      frame_packets_sent_ += count;
    }

    /**
     * @brief Calculate when the next frame may begin after the packets already sent.
     * @return Deadline on the stream's steady clock.
     */
    std::chrono::steady_clock::time_point next_frame_start() const {
      return frame_start_ + std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds {1}) * frame_packets_sent_ / packets_per_millisecond_;
    }

  private:
    const std::size_t packets_per_millisecond_;  ///< Packet budget used by every batch in this frame.
    const std::chrono::steady_clock::time_point frame_start_;  ///< Shared pacing epoch for all FEC blocks of the frame.
    std::size_t frame_packets_sent_ = 0;  ///< Packets sent across all blocks of the frame.
    std::size_t group_packets_sent_ = 0;  ///< Packets sent since the most recent pacing wait.
  };
}  // namespace stream
