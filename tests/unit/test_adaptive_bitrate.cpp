/**
 * @file tests/unit/test_adaptive_bitrate.cpp
 * @brief Test the pure per-session adaptive bitrate controller.
 */

// standard includes
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

// local includes
#include "../tests_common.h"

#include <src/adaptive_bitrate.h>

namespace {
  using namespace std::chrono_literals;
  namespace adaptive = stream::adaptive_bitrate;

  /**
   * @brief Build one deterministic Gate A snapshot for controller tests.
   *
   * @param sequence Per-session publication sequence.
   * @param received_data Data shards received from the network.
   * @param recovered_data Data shards reconstructed by FEC.
   * @param unrecovered_data Data shards that remained missing.
   * @param rtt_ms Smoothed ENet RTT.
   * @param rtt_variance_ms ENet RTT variance.
   * @param idr_requests IDR requests in the window.
   * @return Fully initialized telemetry snapshot.
   */
  stream::network_metrics::snapshot_t make_snapshot(
    const std::uint64_t sequence,
    const std::uint64_t received_data,
    const std::uint64_t recovered_data,
    const std::uint64_t unrecovered_data,
    const std::uint32_t rtt_ms = 20,
    const std::uint32_t rtt_variance_ms = 2,
    const std::uint64_t idr_requests = 0
  ) {
    return {
      .sequence = sequence,
      .window_duration_ms = 500,
      .fec_reports = 1,
      .missing_packets = recovered_data + unrecovered_data,
      .unrecovered_data_packets = unrecovered_data,
      .received_data_packets = received_data,
      .received_parity_packets = recovered_data,
      .fec_recovered_data_packets = recovered_data,
      .idr_requests = idr_requests,
      .malformed_reports = 0,
      .protocol_mismatch_reports = 0,
      .rate_limited_reports = 0,
      .rtt_ms = rtt_ms,
      .rtt_variance_ms = rtt_variance_ms,
    };
  }

  /**
   * @brief Initialize and successfully acknowledge the no-op capability probe.
   *
   * @param controller Controller to activate.
   * @param start Monotonic session start.
   * @param ceiling_kbps Effective client ceiling.
   */
  void activate(
    adaptive::controller_t &controller,
    const adaptive::time_point_t start,
    const std::uint32_t ceiling_kbps = 25'000
  ) {
    controller.initialize({true, ceiling_kbps});
    const auto probe = controller.tick(start, 20, 2);
    ASSERT_TRUE(probe);
    EXPECT_EQ(probe->target_kbps, ceiling_kbps);
    EXPECT_EQ(probe->reason, adaptive::decision_reason_e::capability_probe);
    controller.acknowledge(adaptive::apply_status_e::unchanged, ceiling_kbps, start);
    EXPECT_EQ(controller.state(), adaptive::state_e::monitoring);
  }

  /**
   * @brief Feed two consecutive one-percent unrecovered-loss windows.
   *
   * @param controller Active controller.
   * @param sequence First snapshot sequence.
   * @param first First snapshot time.
   */
  void confirm_unrecovered_loss(
    adaptive::controller_t &controller,
    const std::uint64_t sequence,
    const adaptive::time_point_t first
  ) {
    controller.observe(make_snapshot(sequence, 99, 0, 1), first);
    controller.observe(make_snapshot(sequence + 1, 99, 0, 1), first + 500ms);
  }
}  // namespace

TEST(AdaptiveBitrateControllerTest, DisabledOrInvalidSettingsPreserveFixedBehavior) {
  const adaptive::time_point_t start {};
  adaptive::controller_t disabled;
  disabled.initialize({false, 25'000});

  EXPECT_EQ(disabled.state(), adaptive::state_e::fixed_disabled);
  EXPECT_EQ(disabled.target_kbps(), 25'000U);
  EXPECT_EQ(disabled.ceiling_kbps(), 25'000U);
  EXPECT_FALSE(disabled.tick(start + 1s, 20, 2));

  adaptive::controller_t zero_ceiling;
  zero_ceiling.initialize({true, 0});
  EXPECT_EQ(zero_ceiling.state(), adaptive::state_e::fixed_disabled);
  EXPECT_FALSE(zero_ceiling.tick(start, 20, 2));
}

TEST(AdaptiveBitrateControllerTest, ProbeActivatesOnlyAfterSuccessfulAcknowledgement) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  controller.initialize({true, 25'000});

  EXPECT_EQ(controller.state(), adaptive::state_e::probing);
  const auto probe = controller.tick(start, 20, 2);
  ASSERT_TRUE(probe);
  EXPECT_EQ(probe->target_kbps, 25'000U);
  EXPECT_EQ(probe->reason, adaptive::decision_reason_e::capability_probe);
  EXPECT_EQ(controller.state(), adaptive::state_e::pending);
  EXPECT_FALSE(controller.tick(start + 1s, 20, 2));

  controller.acknowledge(adaptive::apply_status_e::unchanged, 25'000, start + 1s);
  EXPECT_EQ(controller.state(), adaptive::state_e::monitoring);
  EXPECT_EQ(controller.target_kbps(), 25'000U);
}

TEST(AdaptiveBitrateControllerTest, UnsupportedOrFailedProbeFallsBackFixed) {
  const adaptive::time_point_t start {};
  adaptive::controller_t unsupported;
  unsupported.initialize({true, 25'000});
  ASSERT_TRUE(unsupported.tick(start, 20, 2));
  unsupported.acknowledge(adaptive::apply_status_e::unsupported, 25'000, start + 100ms);
  EXPECT_EQ(unsupported.state(), adaptive::state_e::fixed_fallback);
  EXPECT_EQ(unsupported.target_kbps(), 25'000U);

  adaptive::controller_t failed;
  failed.initialize({true, 25'000});
  ASSERT_TRUE(failed.tick(start, 20, 2));
  failed.acknowledge(adaptive::apply_status_e::failed, 25'000, start + 100ms);
  EXPECT_EQ(failed.state(), adaptive::state_e::fixed_fallback);
  EXPECT_EQ(failed.target_kbps(), 25'000U);
}

TEST(AdaptiveBitrateControllerTest, IgnoresAnIsolatedBadWindow) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start);

  controller.observe(make_snapshot(1, 99, 0, 1), start + 500ms);
  EXPECT_TRUE(controller.telemetry_seen());
  EXPECT_FALSE(controller.tick(start + 1s, 20, 2));

  controller.observe(make_snapshot(2, 100, 0, 0), start + 1s);
  EXPECT_FALSE(controller.tick(start + 2s, 20, 2));
  EXPECT_EQ(controller.target_kbps(), 25'000U);
}

TEST(AdaptiveBitrateControllerTest, KeepsConcurrentSessionsIsolated) {
  const adaptive::time_point_t start {};
  adaptive::controller_t first;
  adaptive::controller_t second;
  activate(first, start, 25'000);
  activate(second, start, 12'000);

  confirm_unrecovered_loss(first, 1, start + 500ms);
  const auto first_decision = first.tick(start + 1s, 20, 2);

  ASSERT_TRUE(first_decision);
  EXPECT_EQ(first_decision->target_kbps, 18'700U);
  EXPECT_FALSE(second.telemetry_seen());
  EXPECT_FALSE(second.tick(start + 1s, 20, 2));
  EXPECT_EQ(second.target_kbps(), 12'000U);
  EXPECT_EQ(second.state(), adaptive::state_e::monitoring);
}

TEST(AdaptiveBitrateControllerTest, IgnoresDuplicateAndOutOfOrderSnapshots) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start);
  const auto bad = make_snapshot(2, 99, 0, 1);

  controller.observe(bad, start + 500ms);
  controller.observe(bad, start + 1s);
  controller.observe(make_snapshot(1, 99, 0, 1), start + 1s);
  EXPECT_FALSE(controller.tick(start + 1s, 20, 2));

  controller.observe(make_snapshot(3, 99, 0, 1), start + 1500ms);
  const auto decision = controller.tick(start + 2s, 20, 2);
  ASSERT_TRUE(decision);
  EXPECT_EQ(decision->reason, adaptive::decision_reason_e::unrecovered_loss);
}

TEST(AdaptiveBitrateControllerTest, DecreasesWithinTwoSecondsAfterTwoBadWindows) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start);

  confirm_unrecovered_loss(controller, 1, start + 500ms);
  const auto decision = controller.tick(start + 1s, 20, 2);

  ASSERT_TRUE(decision);
  EXPECT_EQ(decision->reason, adaptive::decision_reason_e::unrecovered_loss);
  EXPECT_EQ(decision->target_kbps, 18'700U);
  controller.acknowledge(adaptive::apply_status_e::applied, decision->target_kbps, start + 1s);
  EXPECT_EQ(controller.target_kbps(), 18'700U);
}

TEST(AdaptiveBitrateControllerTest, EmitsAtMostOneDecisionPerSecond) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start);

  confirm_unrecovered_loss(controller, 1, start + 500ms);
  const auto first = controller.tick(start + 1s, 20, 2);
  ASSERT_TRUE(first);
  controller.acknowledge(adaptive::apply_status_e::applied, first->target_kbps, start + 1s);

  confirm_unrecovered_loss(controller, 3, start + 1100ms);
  EXPECT_FALSE(controller.tick(start + 1500ms, 20, 2));
  const auto second = controller.tick(start + 2s, 20, 2);
  ASSERT_TRUE(second);
  EXPECT_LT(second->target_kbps, first->target_kbps);
}

TEST(AdaptiveBitrateControllerTest, CorroboratesFecPressureButNeverDecreasesForRttAlone) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start);

  controller.observe(make_snapshot(1, 100, 0, 0, 20, 2), start + 500ms);
  controller.observe(make_snapshot(2, 100, 0, 0, 60, 20), start + 1s);
  controller.observe(make_snapshot(3, 100, 0, 0, 60, 20), start + 1500ms);
  EXPECT_FALSE(controller.tick(start + 2s, 60, 20));
  EXPECT_EQ(controller.target_kbps(), 25'000U);

  controller.observe(make_snapshot(4, 95, 5, 0, 60, 20), start + 2500ms);
  controller.observe(make_snapshot(5, 95, 5, 0, 60, 20), start + 3s);
  const auto decision = controller.tick(start + 3s, 60, 20);
  ASSERT_TRUE(decision);
  EXPECT_EQ(decision->reason, adaptive::decision_reason_e::fec_pressure);
  EXPECT_EQ(decision->target_kbps, 21'200U);
}

TEST(AdaptiveBitrateControllerTest, DoesNotDecreaseForFecRecoveryWithoutLatencyCorroboration) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start);

  controller.observe(make_snapshot(1, 100, 0, 0, 20, 2), start + 500ms);
  controller.observe(make_snapshot(2, 95, 5, 0, 20, 2), start + 1s);
  controller.observe(make_snapshot(3, 95, 5, 0, 20, 2), start + 1500ms);

  EXPECT_FALSE(controller.tick(start + 2s, 20, 2));
  EXPECT_EQ(controller.target_kbps(), 25'000U);
}

TEST(AdaptiveBitrateControllerTest, IdrWithAnyUnrecoveredDataUsesSevereDecreasePath) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start);

  controller.observe(make_snapshot(1, 9'999, 0, 1, 20, 2, 1), start + 500ms);
  controller.observe(make_snapshot(2, 9'999, 0, 1, 20, 2, 1), start + 1s);
  const auto decision = controller.tick(start + 1s, 20, 2);

  ASSERT_TRUE(decision);
  EXPECT_EQ(decision->reason, adaptive::decision_reason_e::unrecovered_loss);
  EXPECT_EQ(decision->target_kbps, 18'700U);
}

TEST(AdaptiveBitrateControllerTest, ClampsDecreasesToThreeMbpsOrLowerClientCeiling) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start, 4'000);

  std::uint64_t sequence = 1;
  auto now = start + 500ms;
  for (int attempt = 0; attempt < 4; ++attempt) {
    confirm_unrecovered_loss(controller, sequence, now);
    sequence += 2;
    now += 1s;
    const auto decision = controller.tick(now, 20, 2);
    if (decision) {
      controller.acknowledge(adaptive::apply_status_e::applied, decision->target_kbps, now);
    }
  }
  EXPECT_EQ(controller.target_kbps(), adaptive::minimum_target_kbps);

  adaptive::controller_t low_ceiling;
  activate(low_ceiling, start, 2'500);
  confirm_unrecovered_loss(low_ceiling, 1, start + 500ms);
  EXPECT_FALSE(low_ceiling.tick(start + 1s, 20, 2));
  EXPECT_EQ(low_ceiling.target_kbps(), 2'500U);
}

TEST(AdaptiveBitrateControllerTest, IncreasesOnlyAfterTenStableSecondsAndThenEveryTwoSeconds) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start);
  confirm_unrecovered_loss(controller, 1, start + 500ms);
  const auto decrease = controller.tick(start + 1s, 20, 2);
  ASSERT_TRUE(decrease);
  controller.acknowledge(adaptive::apply_status_e::applied, decrease->target_kbps, start + 1s);

  EXPECT_FALSE(controller.tick(start + 10'999ms, 20, 2));
  const auto first_increase = controller.tick(start + 11s, 20, 2);
  ASSERT_TRUE(first_increase);
  EXPECT_EQ(first_increase->reason, adaptive::decision_reason_e::stable_recovery);
  EXPECT_EQ(first_increase->target_kbps, decrease->target_kbps + 1'250U);
  controller.acknowledge(adaptive::apply_status_e::applied, first_increase->target_kbps, start + 11s);

  EXPECT_FALSE(controller.tick(start + 12'999ms, 20, 2));
  const auto second_increase = controller.tick(start + 13s, 20, 2);
  ASSERT_TRUE(second_increase);
  EXPECT_EQ(second_increase->target_kbps, first_increase->target_kbps + 1'250U);
}

TEST(AdaptiveBitrateControllerTest, BoundsReversalsWithoutDelayingSafetyDecreases) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start);

  confirm_unrecovered_loss(controller, 1, start + 500ms);
  const auto first_decrease = controller.tick(start + 1s, 20, 2);
  ASSERT_TRUE(first_decrease);
  controller.acknowledge(adaptive::apply_status_e::applied, first_decrease->target_kbps, start + 1s);

  const auto first_increase = controller.tick(start + 11s, 20, 2);
  ASSERT_TRUE(first_increase);
  controller.acknowledge(adaptive::apply_status_e::applied, first_increase->target_kbps, start + 11s);

  confirm_unrecovered_loss(controller, 3, start + 11'500ms);
  const auto safety_decrease = controller.tick(start + 12s, 20, 2);
  ASSERT_TRUE(safety_decrease);
  EXPECT_LT(safety_decrease->target_kbps, first_increase->target_kbps);
  controller.acknowledge(adaptive::apply_status_e::applied, safety_decrease->target_kbps, start + 12s);

  EXPECT_FALSE(controller.tick(start + 22s, 20, 2));
  EXPECT_FALSE(controller.tick(start + 42s, 20, 2));
  const auto delayed_increase = controller.tick(start + 42s + 1ms, 20, 2);
  ASSERT_TRUE(delayed_increase);
  EXPECT_EQ(delayed_increase->reason, adaptive::decision_reason_e::stable_recovery);
  controller.acknowledge(adaptive::apply_status_e::applied, delayed_increase->target_kbps, start + 42s + 1ms);

  confirm_unrecovered_loss(controller, 5, start + 42s + 501ms);
  const auto next_safety_decrease = controller.tick(start + 43s + 1ms, 20, 2);
  ASSERT_TRUE(next_safety_decrease);
  EXPECT_LT(next_safety_decrease->target_kbps, delayed_increase->target_kbps);
}

TEST(AdaptiveBitrateControllerTest, CapacityBoundedTraceStaysBelowThreeReversalsPerThirtySeconds) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start, 15'000);

  std::uint64_t sequence = 1;
  int previous_direction = 0;
  std::vector<adaptive::time_point_t> reversal_times;

  for (int half_second = 1; half_second <= 200; ++half_second) {
    const auto now = start + half_second * 500ms;
    const auto constrained = controller.target_kbps() > 12'000;
    controller.observe(
      make_snapshot(sequence++, constrained ? 98 : 100, 0, constrained ? 2 : 0, constrained ? 45 : 20, constrained ? 20 : 2),
      now
    );

    const auto decision = controller.tick(now, constrained ? 45 : 20, constrained ? 20 : 2);
    if (!decision) {
      continue;
    }

    const auto direction = decision->target_kbps < controller.target_kbps() ? -1 : 1;
    if (previous_direction != 0 && direction != previous_direction) {
      reversal_times.push_back(now);
    }
    previous_direction = direction;
    controller.acknowledge(adaptive::apply_status_e::applied, decision->target_kbps, now);
  }

  ASSERT_GE(reversal_times.size(), 6U);
  for (std::size_t first = 0; first < reversal_times.size(); ++first) {
    std::size_t reversals_in_closed_window = 1;
    for (auto next = first + 1;
         next < reversal_times.size() && reversal_times[next] - reversal_times[first] <= 30s;
         ++next) {
      ++reversals_in_closed_window;
    }
    EXPECT_LT(reversals_in_closed_window, 3U);
  }
  EXPECT_LE(controller.target_kbps(), controller.ceiling_kbps());
}

TEST(AdaptiveBitrateControllerTest, InvalidTelemetryOrFailedApplyKeepsLastEffectiveTarget) {
  const adaptive::time_point_t start {};
  adaptive::controller_t invalid_telemetry;
  activate(invalid_telemetry, start);
  auto invalid = make_snapshot(1, 100, 0, 0);
  invalid.malformed_reports = 1;
  invalid_telemetry.observe(invalid, start + 500ms);

  EXPECT_EQ(invalid_telemetry.state(), adaptive::state_e::fixed_fallback);
  EXPECT_EQ(invalid_telemetry.target_kbps(), 25'000U);
  EXPECT_FALSE(invalid_telemetry.tick(start + 2s, 20, 2));

  adaptive::controller_t failed_apply;
  activate(failed_apply, start);
  confirm_unrecovered_loss(failed_apply, 1, start + 500ms);
  const auto decrease = failed_apply.tick(start + 1s, 20, 2);
  ASSERT_TRUE(decrease);
  failed_apply.acknowledge(adaptive::apply_status_e::failed, 25'000, start + 1s);
  EXPECT_EQ(failed_apply.state(), adaptive::state_e::fixed_fallback);
  EXPECT_EQ(failed_apply.target_kbps(), 25'000U);
}

TEST(AdaptiveBitrateControllerTest, InvalidTelemetryDefersFallbackUntilInflightAcknowledgement) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start);
  confirm_unrecovered_loss(controller, 1, start + 500ms);
  const auto decrease = controller.tick(start + 1s, 20, 2);
  ASSERT_TRUE(decrease);

  auto invalid = make_snapshot(3, 100, 0, 0);
  invalid.rate_limited_reports = 1;
  controller.observe(invalid, start + 1100ms);
  EXPECT_EQ(controller.state(), adaptive::state_e::pending);

  controller.acknowledge(adaptive::apply_status_e::applied, decrease->target_kbps, start + 1200ms);
  EXPECT_EQ(controller.state(), adaptive::state_e::fixed_fallback);
  EXPECT_EQ(controller.target_kbps(), decrease->target_kbps);
}

TEST(AdaptiveBitrateControllerTest, SimulationRecoversAcrossSixtyThirtySixtySecondTrace) {
  const adaptive::time_point_t start {};
  adaptive::controller_t controller;
  activate(controller, start);

  std::uint64_t sequence = 1;
  std::optional<adaptive::time_point_t> first_decrease_at;
  int decreases = 0;
  int increases = 0;
  int reversals = 0;
  int previous_direction = 0;

  /** Record and immediately acknowledge a deterministic controller command. */
  const auto apply_decision = [&](const adaptive::time_point_t now, const std::optional<adaptive::decision_t> &decision) {
    if (!decision) {
      return;
    }

    const auto direction = decision->target_kbps < controller.target_kbps() ? -1 : 1;
    if (previous_direction != 0 && direction != previous_direction) {
      ++reversals;
    }
    previous_direction = direction;
    if (direction < 0) {
      ++decreases;
      if (!first_decrease_at) {
        first_decrease_at = now;
      }
    } else {
      ++increases;
    }
    controller.acknowledge(adaptive::apply_status_e::applied, decision->target_kbps, now);
  };

  for (int half_second = 1; half_second <= 120; ++half_second) {
    const auto now = start + half_second * 500ms;
    apply_decision(now, controller.tick(now, 20, 2));
  }
  EXPECT_GE(controller.target_kbps(), 23'750U);

  for (int half_second = 121; half_second <= 180; ++half_second) {
    const auto now = start + half_second * 500ms;
    controller.observe(make_snapshot(sequence++, 98, 0, 2, 45, 20, 1), now);
    apply_decision(now, controller.tick(now, 45, 20));
  }

  ASSERT_TRUE(first_decrease_at);
  EXPECT_LT(*first_decrease_at - (start + 60s), 2s);
  EXPECT_GT(decreases, 0);

  for (int half_second = 181; half_second <= 300; ++half_second) {
    const auto now = start + half_second * 500ms;
    apply_decision(now, controller.tick(now, 20, 2));
  }

  EXPECT_GT(increases, 0);
  EXPECT_LT(reversals, 3);
  EXPECT_EQ(controller.target_kbps(), 25'000U);
  EXPECT_LE(controller.target_kbps(), controller.ceiling_kbps());
}
