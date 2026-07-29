/**
 * @file src/adaptive_bitrate.cpp
 * @brief Definitions for the per-session adaptive bitrate controller.
 */

// standard includes
#include <algorithm>
#include <chrono>
#include <cstdint>

// local includes
#include "adaptive_bitrate.h"

namespace stream::adaptive_bitrate {
  namespace {
    using namespace std::chrono_literals;

    /** Minimum spacing between any two encoder commands. */
    constexpr auto decision_interval = 1s;

    /** Maximum span in which two degraded telemetry windows confirm pressure. */
    constexpr auto bad_window_confirmation_span = 1500ms;

    /** Stable time required before the first recovery increase. */
    constexpr auto stable_recovery_delay = 10s;

    /** Minimum spacing between recovery increases. */
    constexpr auto recovery_increase_interval = 2s;

    /** Cooldown after an oscillation before another upward reversal is allowed. */
    constexpr auto upward_reversal_cooldown = 30s;

    /** Unrecovered data ratio that marks a severe window. */
    constexpr long double unrecovered_loss_threshold = 0.01L;

    /** FEC recovery ratio that marks a pressure window when latency agrees. */
    constexpr long double fec_pressure_threshold = 0.05L;

    /** FEC recovery ratio below which a window may be considered stable. */
    constexpr long double stable_fec_threshold = 0.01L;

    /**
     * @brief Compute a packet ratio without overflowing aggregate counters.
     *
     * @param numerator Packet count of interest.
     * @param snapshot Telemetry window providing the observed data counts.
     * @return Ratio over received, FEC-recovered, and unrecovered data shards.
     */
    long double data_ratio(const std::uint64_t numerator, const network_metrics::snapshot_t &snapshot) {
      const auto denominator = static_cast<long double>(snapshot.received_data_packets) +
                               static_cast<long double>(snapshot.fec_recovered_data_packets) +
                               static_cast<long double>(snapshot.unrecovered_data_packets);
      return denominator == 0.0L ? 0.0L : static_cast<long double>(numerator) / denominator;
    }

    /**
     * @brief Determine whether latency corroborates packet-recovery pressure.
     *
     * RTT must be both 50% and 15 ms above the stable-path floor. Variance is
     * high at 10 ms or half the baseline, whichever is larger.
     *
     * @param rtt_ms Current smoothed RTT.
     * @param rtt_variance_ms Current RTT variance.
     * @param baseline_rtt_ms Stable-path RTT floor.
     * @return `true` when RTT inflation or variance is materially elevated.
     */
    bool latency_is_high(
      const std::uint32_t rtt_ms,
      const std::uint32_t rtt_variance_ms,
      const std::uint32_t baseline_rtt_ms
    ) {
      if (baseline_rtt_ms == 0) {
        return false;
      }

      const auto inflated_rtt = rtt_ms != 0 &&
                                static_cast<std::uint64_t>(rtt_ms) >= static_cast<std::uint64_t>(baseline_rtt_ms) + 15 &&
                                static_cast<std::uint64_t>(rtt_ms) * 2 >= static_cast<std::uint64_t>(baseline_rtt_ms) * 3;
      const auto high_variance = rtt_variance_ms >= std::max<std::uint32_t>(10, baseline_rtt_ms / 2);
      return inflated_rtt || high_variance;
    }

    /**
     * @brief Check whether an interval has fully elapsed under a monotonic clock.
     *
     * @tparam Duration Duration type accepted by the comparison.
     * @param now Current monotonic time.
     * @param since Start time.
     * @param interval Required interval.
     * @return `true` once the complete interval has elapsed.
     */
    template<typename Duration>
    bool elapsed(const time_point_t now, const time_point_t since, const Duration interval) {
      return now >= since && now - since >= interval;
    }
  }  // namespace

  void controller_t::initialize(const settings_t settings) {
    state_ = settings.enabled && settings.ceiling_kbps != 0 ? state_e::probing : state_e::fixed_disabled;
    ceiling_kbps_ = settings.ceiling_kbps;
    floor_kbps_ = std::min(minimum_target_kbps, ceiling_kbps_);
    target_kbps_ = ceiling_kbps_;
    pending_target_kbps_ = 0;
    pending_reason_ = decision_reason_e::capability_check;
    baseline_rtt_ms_ = 0;
    last_control_liveness_token_ = 0;
    last_snapshot_sequence_ = 0;
    consecutive_bad_windows_ = 0;
    candidate_degradation_ = degradation_e::none;
    pending_degradation_ = degradation_e::none;
    telemetry_seen_ = false;
    fallback_after_acknowledgement_ = false;
    recovery_blocked_by_rate_limit_ = false;
    first_bad_window_at_.reset();
    stable_since_.reset();
    decision_history_ = {};
  }

  bool controller_t::handle_invalid_telemetry(const network_metrics::snapshot_t &snapshot) {
    if (snapshot.malformed_reports == 0 && snapshot.protocol_mismatch_reports == 0) {
      return false;
    }

    if (state_ == state_e::pending) {
      fallback_after_acknowledgement_ = true;
    } else {
      state_ = state_e::fixed_fallback;
      pending_target_kbps_ = 0;
    }
    return true;
  }

  bool controller_t::handle_rate_limited_telemetry(const network_metrics::snapshot_t &snapshot) {
    if (snapshot.rate_limited_reports == 0) {
      return false;
    }

    reset_degradation_confirmation();
    pending_degradation_ = degradation_e::none;
    stable_since_.reset();
    recovery_blocked_by_rate_limit_ = true;
    return true;
  }

  bool controller_t::accept_telemetry_signal(const network_metrics::snapshot_t &snapshot) {
    if (snapshot.fec_reports != 0) {
      telemetry_seen_ = true;
      return true;
    }
    return telemetry_seen_ || snapshot.frame_loss_requests != 0;
  }

  controller_t::degradation_e controller_t::classify_degradation(
    const network_metrics::snapshot_t &snapshot,
    const long double unrecovered_ratio,
    const long double recovered_ratio,
    const bool high_latency
  ) {
    using enum degradation_e;

    if (const auto severe_loss = unrecovered_ratio >= unrecovered_loss_threshold ||
                                 snapshot.incomplete_fec_reports != 0 ||
                                 snapshot.frame_loss_requests != 0;
        severe_loss) {
      return unrecovered_loss;
    }
    if (recovered_ratio >= fec_pressure_threshold && high_latency) {
      return fec_pressure;
    }
    return none;
  }

  void controller_t::reset_degradation_confirmation() {
    consecutive_bad_windows_ = 0;
    candidate_degradation_ = degradation_e::none;
    first_bad_window_at_.reset();
  }

  void controller_t::observe_degradation(const degradation_e degradation, const time_point_t now) {
    using enum degradation_e;

    stable_since_ = now;

    if (!first_bad_window_at_ || now < *first_bad_window_at_ ||
        now - *first_bad_window_at_ > bad_window_confirmation_span) {
      first_bad_window_at_ = now;
      consecutive_bad_windows_ = 1;
      candidate_degradation_ = degradation;
      return;
    }

    ++consecutive_bad_windows_;
    if (degradation == unrecovered_loss) {
      candidate_degradation_ = unrecovered_loss;
    }

    if (consecutive_bad_windows_ < 2) {
      return;
    }

    if (candidate_degradation_ == unrecovered_loss || degradation == unrecovered_loss) {
      pending_degradation_ = unrecovered_loss;
    } else if (pending_degradation_ == none) {
      pending_degradation_ = fec_pressure;
    }
    reset_degradation_confirmation();
  }

  void controller_t::observe_stability(
    const network_metrics::snapshot_t &snapshot,
    const long double recovered_ratio,
    const bool high_latency,
    const time_point_t now
  ) {
    reset_degradation_confirmation();

    const auto stable_window = snapshot.unrecovered_data_packets == 0 &&
                               snapshot.incomplete_fec_reports == 0 &&
                               snapshot.frame_loss_requests == 0 &&
                               recovered_ratio < stable_fec_threshold &&
                               !high_latency;
    if (!stable_window || !stable_since_) {
      stable_since_ = now;
    }

    if (stable_window && snapshot.fec_reports != 0) {
      if (recovery_blocked_by_rate_limit_) {
        stable_since_ = now;
      }
      recovery_blocked_by_rate_limit_ = false;
    }

    if (stable_window && snapshot.rtt_ms != 0 &&
        (baseline_rtt_ms_ == 0 || snapshot.rtt_ms < baseline_rtt_ms_)) {
      baseline_rtt_ms_ = snapshot.rtt_ms;
    }
  }

  void controller_t::observe(const network_metrics::snapshot_t &snapshot, const time_point_t now) {
    if (state_ == state_e::fixed_disabled || state_ == state_e::fixed_fallback ||
        snapshot.sequence <= last_snapshot_sequence_) {
      return;
    }
    last_snapshot_sequence_ = snapshot.sequence;

    if (handle_invalid_telemetry(snapshot) ||
        handle_rate_limited_telemetry(snapshot) ||
        !accept_telemetry_signal(snapshot)) {
      return;
    }

    if (baseline_rtt_ms_ == 0 && snapshot.rtt_ms != 0) {
      baseline_rtt_ms_ = snapshot.rtt_ms;
    }

    const auto unrecovered_ratio = data_ratio(snapshot.unrecovered_data_packets, snapshot);
    const auto recovered_ratio = data_ratio(snapshot.fec_recovered_data_packets, snapshot);
    const auto high_latency = latency_is_high(snapshot.rtt_ms, snapshot.rtt_variance_ms, baseline_rtt_ms_);
    if (const auto degradation = classify_degradation(snapshot, unrecovered_ratio, recovered_ratio, high_latency);
        degradation != degradation_e::none) {
      observe_degradation(degradation, now);
      return;
    }

    observe_stability(snapshot, recovered_ratio, high_latency, now);
  }

  bool controller_t::consume_control_liveness(const std::uint32_t control_liveness_token) {
    const auto fresh_control_liveness =
      control_liveness_token != 0 && control_liveness_token != last_control_liveness_token_;
    if (fresh_control_liveness) {
      last_control_liveness_token_ = control_liveness_token;
    }
    return fresh_control_liveness;
  }

  bool controller_t::observe_live_latency(
    const time_point_t now,
    const std::uint32_t live_rtt_ms,
    const std::uint32_t live_rtt_variance_ms
  ) {
    if (baseline_rtt_ms_ == 0 && live_rtt_ms != 0) {
      baseline_rtt_ms_ = live_rtt_ms;
    }
    const auto high_latency = latency_is_high(live_rtt_ms, live_rtt_variance_ms, baseline_rtt_ms_);
    if (high_latency) {
      stable_since_ = now;
    } else if (live_rtt_ms != 0 && live_rtt_ms < baseline_rtt_ms_) {
      baseline_rtt_ms_ = live_rtt_ms;
    }
    return high_latency;
  }

  bool controller_t::decision_is_allowed(const time_point_t now) const {
    return !decision_history_.last_decision_at ||
           elapsed(now, *decision_history_.last_decision_at, decision_interval);
  }

  decision_t controller_t::emit_decision(
    const std::uint32_t target_kbps,
    const decision_reason_e reason,
    const time_point_t now
  ) {
    using enum decision_direction_e;

    auto direction = none;
    if (target_kbps < target_kbps_) {
      direction = decrease;
    } else if (target_kbps > target_kbps_) {
      direction = increase;
    }

    if (direction != none) {
      if (decision_history_.direction != none && direction != decision_history_.direction) {
        decision_history_.last_direction_reversal_at = now;
      }
      decision_history_.direction = direction;
    }

    pending_target_kbps_ = target_kbps;
    pending_reason_ = reason;
    decision_history_.last_decision_at = now;
    state_ = state_e::pending;
    return {target_kbps, reason};
  }

  std::optional<decision_t> controller_t::reduce_for_pending_degradation(
    const time_point_t now,
    const bool decision_allowed
  ) {
    if (pending_degradation_ == degradation_e::none || !decision_allowed) {
      return std::nullopt;
    }

    if (target_kbps_ <= floor_kbps_) {
      pending_degradation_ = degradation_e::none;
      return std::nullopt;
    }

    const auto percentage = pending_degradation_ == degradation_e::unrecovered_loss ? 75U : 85U;
    auto reduced = static_cast<std::uint32_t>(static_cast<std::uint64_t>(target_kbps_) * percentage / 100U);
    reduced = reduced / 100U * 100U;
    reduced = std::max(reduced, floor_kbps_);
    const auto reason = pending_degradation_ == degradation_e::unrecovered_loss ?
                          decision_reason_e::unrecovered_loss :
                          decision_reason_e::fec_pressure;
    pending_degradation_ = degradation_e::none;
    if (reduced >= target_kbps_) {
      return std::nullopt;
    }
    return emit_decision(reduced, reason, now);
  }

  bool controller_t::recovery_is_allowed(
    const time_point_t now,
    const bool live_latency_high,
    const bool fresh_control_liveness,
    const bool decision_allowed
  ) const {
    return telemetry_seen_ &&
           stable_since_.has_value() &&
           target_kbps_ < ceiling_kbps_ &&
           !live_latency_high &&
           fresh_control_liveness &&
           !recovery_blocked_by_rate_limit_ &&
           elapsed(now, *stable_since_, stable_recovery_delay) &&
           (!decision_history_.last_increase_at ||
            elapsed(now, *decision_history_.last_increase_at, recovery_increase_interval)) &&
           decision_allowed;
  }

  bool controller_t::upward_reversal_is_cooling_down(const time_point_t now) const {
    return decision_history_.direction == decision_direction_e::decrease &&
           decision_history_.last_direction_reversal_at.has_value() &&
           (now <= *decision_history_.last_direction_reversal_at ||
            now - *decision_history_.last_direction_reversal_at <= upward_reversal_cooldown);
  }

  std::optional<decision_t> controller_t::tick(
    const time_point_t now,
    const std::uint32_t live_rtt_ms,
    const std::uint32_t live_rtt_variance_ms,
    const std::uint32_t control_liveness_token
  ) {
    const auto fresh_control_liveness = consume_control_liveness(control_liveness_token);

    if (state_ == state_e::fixed_disabled || state_ == state_e::fixed_fallback) {
      return std::nullopt;
    }

    if (state_ == state_e::probing) {
      return emit_decision(ceiling_kbps_, decision_reason_e::capability_check, now);
    }

    if (state_ == state_e::pending) {
      return std::nullopt;
    }

    const auto live_latency_high = observe_live_latency(now, live_rtt_ms, live_rtt_variance_ms);
    const auto decision_allowed = decision_is_allowed(now);
    if (auto decrease = reduce_for_pending_degradation(now, decision_allowed)) {
      return decrease;
    }

    if (!recovery_is_allowed(now, live_latency_high, fresh_control_liveness, decision_allowed) ||
        upward_reversal_is_cooling_down(now)) {
      return std::nullopt;
    }

    const auto increase_step = std::max<std::uint32_t>(500, ceiling_kbps_ / 20);
    const auto room = ceiling_kbps_ - target_kbps_;
    const auto increased = target_kbps_ + std::min(increase_step, room);
    return emit_decision(increased, decision_reason_e::stable_recovery, now);
  }

  void controller_t::acknowledge(
    const apply_status_e status,
    const std::uint32_t effective_target_kbps,
    const time_point_t now
  ) {
    using enum decision_reason_e;

    if (state_ != state_e::pending) {
      return;
    }

    const auto successful = status == apply_status_e::applied || status == apply_status_e::unchanged;
    if (effective_target_kbps != 0) {
      target_kbps_ = std::clamp(effective_target_kbps, floor_kbps_, ceiling_kbps_);
    } else if (successful) {
      target_kbps_ = pending_target_kbps_;
    }

    if (successful && !fallback_after_acknowledgement_) {
      if (pending_reason_ == stable_recovery) {
        decision_history_.last_increase_at = now;
      } else if (pending_reason_ == unrecovered_loss || pending_reason_ == fec_pressure) {
        stable_since_ = now;
      }
      state_ = state_e::monitoring;
    } else {
      state_ = state_e::fixed_fallback;
    }

    fallback_after_acknowledgement_ = false;
    pending_target_kbps_ = 0;
  }

  state_e controller_t::state() const {
    return state_;
  }

  std::uint32_t controller_t::target_kbps() const {
    return target_kbps_;
  }

  std::uint32_t controller_t::ceiling_kbps() const {
    return ceiling_kbps_;
  }

  bool controller_t::telemetry_seen() const {
    return telemetry_seen_;
  }
}  // namespace stream::adaptive_bitrate
