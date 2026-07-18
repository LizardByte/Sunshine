/**
 * @file src/adaptive_bitrate.h
 * @brief Declarations for the per-session adaptive bitrate controller.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <optional>

// local includes
#include "network_metrics.h"

namespace stream::adaptive_bitrate {
  /** Monotonic time point used by the controller. */
  using time_point_t = std::chrono::steady_clock::time_point;

  /** Lowest adaptive target, unless the client ceiling itself is lower. */
  inline constexpr std::uint32_t minimum_target_kbps = 3'000;

  /**
   * @brief Lifecycle state of one per-session controller.
   */
  enum class state_e {
    fixed_disabled,  ///< Adaptation is disabled and the client target is left unchanged.
    probing,  ///< The controller is waiting to send its no-op backend capability check.
    monitoring,  ///< Telemetry is monitored and bitrate decisions may be emitted.
    pending,  ///< One encoder request is awaiting acknowledgement.
    fixed_fallback,  ///< Adaptation stopped and the last effective target is retained.
  };

  /**
   * @brief Stable reason attached to an emitted bitrate decision.
   */
  enum class decision_reason_e {
    capability_check,  ///< Check backend support without changing the initial target.
    unrecovered_loss,  ///< Reduce after consecutive windows with unrecovered loss.
    fec_pressure,  ///< Reduce after consecutive FEC-heavy windows corroborated by latency.
    stable_recovery,  ///< Increase after a sustained stable recovery period.
  };

  /**
   * @brief Encoder outcome understood by the controller.
   *
   * This deliberately mirrors, but does not depend on, the video backend result
   * enum so the controller remains a small, platform-independent component.
   */
  enum class apply_status_e {
    applied,  ///< The requested target was applied.
    unchanged,  ///< The backend was already using the requested target.
    invalid,  ///< The backend rejected the target as invalid.
    unsupported,  ///< Runtime bitrate changes are unsupported by the backend.
    failed,  ///< The backend attempted and failed to apply the target.
  };

  /**
   * @brief Immutable settings copied into one stream session.
   */
  struct settings_t {
    bool enabled = false;  ///< Opt-in switch; disabled preserves fixed bitrate behavior.
    std::uint32_t ceiling_kbps = 0;  ///< Effective client video bitrate ceiling in kilobits per second.
  };

  /**
   * @brief One encoder command emitted by the controller.
   */
  struct decision_t {
    std::uint32_t target_kbps;  ///< Requested encoder target in kilobits per second.
    decision_reason_e reason;  ///< Stable diagnostic reason for the command.
  };

  /**
   * @brief Pure O(1), per-session adaptive bitrate state machine.
   *
   * The owner feeds every published FEC telemetry snapshot to observe(),
   * calls tick() from its control loop, and returns encoder results through
   * acknowledge(). The class performs no I/O, allocation, locking, or clock
   * reads of its own.
   */
  class controller_t {
  public:
    /**
     * @brief Reset the controller for a new stream session.
     *
     * An enabled controller starts by probing runtime encoder support at the
     * unchanged client ceiling. A disabled or zero-ceiling controller stays in
     * fixed mode.
     *
     * @param settings Per-session opt-in and effective client ceiling.
     */
    void initialize(settings_t settings);

    /**
     * @brief Consume one newly published FEC telemetry window.
     *
     * Duplicate or out-of-order snapshot sequence numbers are ignored.
     * Malformed or protocol-mismatched telemetry moves an active controller to
     * fixed fallback at its last effective target, after any in-flight encoder
     * command is acknowledged. A rate-limited window instead holds the current
     * target and clears accumulated degradation and recovery evidence. Recovery
     * remains blocked until a later valid, stable FEC report makes event silence
     * trustworthy again. An already outstanding command remains pending and may
     * still be acknowledged.
     *
     * @param snapshot Immutable network telemetry aggregate.
     * @param now Monotonic observation time.
     */
    void observe(const network_metrics::snapshot_t &snapshot, time_point_t now);

    /**
     * @brief Advance timers and optionally emit one encoder command.
     *
     * At most one command is emitted per second and at most one command may be
     * outstanding until the synchronous encoder result is acknowledged. RTT by
     * itself never causes a decrease, but elevated live RTT or variance prevents
     * an increase. Recovery also requires a nonzero control-liveness token that
     * differs from the most recently observed token. Sunshine supplies ENet's
     * last-receive time, which is refreshed in the same acknowledgement handler
     * as the smoothed RTT sample. Consequently, elapsed wall-clock time and stale
     * RTT values cannot cause upward recovery without a newly received RTT sample.
     *
     * @param now Monotonic control-loop time.
     * @param live_rtt_ms Current ENet smoothed round-trip time.
     * @param live_rtt_variance_ms Current ENet round-trip-time variance.
     * @param control_liveness_token Latest control-channel receive token, such
     * as ENet's peer last-receive time. Zero means no valid liveness sample.
     * @return A command to send to the encoder, or no value.
     */
    [[nodiscard]] std::optional<decision_t> tick(
      time_point_t now,
      std::uint32_t live_rtt_ms,
      std::uint32_t live_rtt_variance_ms,
      std::uint32_t control_liveness_token
    );

    /**
     * @brief Complete the currently outstanding encoder command.
     *
     * Successful probe acknowledgement activates monitoring. Any invalid,
     * unsupported, or failed result enters fixed fallback. Acknowledgements
     * received without an outstanding command are ignored.
     *
     * @param status Backend result mapped to the controller enum.
     * @param effective_target_kbps Backend target after the attempt, or zero when unknown.
     * @param now Monotonic acknowledgement time.
     */
    void acknowledge(apply_status_e status, std::uint32_t effective_target_kbps, time_point_t now);

    /**
     * @brief Return the current lifecycle state.
     *
     * @return Current state.
     */
    [[nodiscard]] state_e state() const;

    /**
     * @brief Return the last known effective encoder target.
     *
     * @return Effective target in kilobits per second.
     */
    [[nodiscard]] std::uint32_t target_kbps() const;

    /**
     * @brief Return the immutable client ceiling for this session.
     *
     * @return Effective client ceiling in kilobits per second.
     */
    [[nodiscard]] std::uint32_t ceiling_kbps() const;

    /**
     * @brief Return whether at least one valid FEC report was observed.
     *
     * @return `true` after valid event-driven telemetry has been seen.
     */
    [[nodiscard]] bool telemetry_seen() const;

  private:
    /** Direction of the latest non-probe encoder command. */
    enum class decision_direction_e {
      none,
      decrease,
      increase,
    };

    /** Severity retained while confirming consecutive degraded windows. */
    enum class degradation_e {
      none,
      fec_pressure,
      unrecovered_loss,
    };

    state_e state_ = state_e::fixed_disabled;  ///< Current lifecycle state.
    std::uint32_t floor_kbps_ = 0;  ///< Session floor, capped by the client ceiling.
    std::uint32_t ceiling_kbps_ = 0;  ///< Immutable effective client ceiling.
    std::uint32_t target_kbps_ = 0;  ///< Last known effective target.
    std::uint32_t pending_target_kbps_ = 0;  ///< Target awaiting encoder acknowledgement.
    decision_reason_e pending_reason_ = decision_reason_e::capability_check;  ///< Outstanding command reason.
    std::uint32_t baseline_rtt_ms_ = 0;  ///< Stable-path RTT floor used only as corroboration.
    std::uint32_t last_control_liveness_token_ = 0;  ///< Most recently consumed control-channel RTT sample token.
    std::uint64_t last_snapshot_sequence_ = 0;  ///< Last consumed FEC telemetry snapshot sequence.
    std::uint8_t consecutive_bad_windows_ = 0;  ///< Consecutive degraded windows awaiting confirmation.
    degradation_e candidate_degradation_ = degradation_e::none;  ///< Strongest unconfirmed degradation.
    degradation_e pending_degradation_ = degradation_e::none;  ///< Confirmed degradation awaiting a decision.
    bool telemetry_seen_ = false;  ///< Whether a valid FEC report has been observed.
    bool fallback_after_acknowledgement_ = false;  ///< Defer invalid-telemetry fallback while an encoder command is in flight.
    bool recovery_blocked_by_rate_limit_ = false;  ///< Require a valid stable FEC report after telemetry overload.
    decision_direction_e last_decision_direction_ = decision_direction_e::none;  ///< Latest non-probe command direction.
    std::optional<time_point_t> first_bad_window_at_;  ///< Time of the first consecutive bad window.
    std::optional<time_point_t> stable_since_;  ///< Start of the current no-new-pressure period.
    std::optional<time_point_t> last_decision_at_;  ///< Time of the last emitted encoder command.
    std::optional<time_point_t> last_increase_at_;  ///< Time of the last recovery increase.
    std::optional<time_point_t> last_direction_reversal_at_;  ///< Latest non-probe command direction reversal.
  };
}  // namespace stream::adaptive_bitrate
