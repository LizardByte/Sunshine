/**
 * @file src/nvenc/nvenc_reconfigure.cpp
 * @brief Definitions for atomic NVENC bitrate reconfiguration state.
 */

// standard includes
#include <limits>
#include <optional>

// local includes
#include "nvenc_reconfigure.h"

namespace NVENC_NAMESPACE {

  void bitrate_reconfigure_state_t::initialize(
    bool supported,
    const NV_ENC_INITIALIZE_PARAMS &init_params,
    const NV_ENC_CONFIG &encode_config
  ) {
    supported_ = supported;
    init_params_ = init_params;
    encode_config_ = encode_config;
    init_params_.encodeConfig = &encode_config_;
    baseline_average_bitrate_ = encode_config.rcParams.averageBitRate;
    baseline_vbv_buffer_size_ = encode_config.rcParams.vbvBufferSize;
    baseline_vbv_initial_delay_ = encode_config.rcParams.vbvInitialDelay;
  }

  void bitrate_reconfigure_state_t::reset() {
    supported_ = false;
    init_params_ = {};
    encode_config_ = {};
    baseline_average_bitrate_ = 0;
    baseline_vbv_buffer_size_ = 0;
    baseline_vbv_initial_delay_ = 0;
  }

  video::bitrate_reconfigure_result_t bitrate_reconfigure_state_t::reconfigure(
    std::uint32_t target_kbps,
    const apply_function_t &apply
  ) {
    const auto old_target_kbps = encode_config_.rcParams.averageBitRate / 1000;
    auto result = video::bitrate_reconfigure_result_t {
      video::bitrate_reconfigure_status_e::unsupported,
      old_target_kbps,
      target_kbps,
      old_target_kbps,
    };

    if (!supported_) {
      return result;
    }

    if (target_kbps == 0 || target_kbps > std::numeric_limits<std::uint32_t>::max() / 1000) {
      result.status = video::bitrate_reconfigure_status_e::invalid;
      return result;
    }

    const auto target_bps = target_kbps * 1000;
    if (target_bps == encode_config_.rcParams.averageBitRate) {
      result.status = video::bitrate_reconfigure_status_e::unchanged;
      return result;
    }

    if (baseline_average_bitrate_ == 0 || !apply) {
      result.status = video::bitrate_reconfigure_status_e::failed;
      return result;
    }

    auto scaled_from_baseline = [&](std::uint32_t baseline_value) -> std::optional<std::uint32_t> {
      const auto scaled = static_cast<std::uint64_t>(baseline_value) * target_bps / baseline_average_bitrate_;
      if (scaled > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
      }
      return static_cast<std::uint32_t>(scaled);
    };

    auto next_config = encode_config_;
    next_config.rcParams.averageBitRate = target_bps;
    next_config.rcParams.maxBitRate = target_bps;
    if (baseline_vbv_buffer_size_ != 0) {
      auto scaled_vbv = scaled_from_baseline(baseline_vbv_buffer_size_);
      if (!scaled_vbv) {
        result.status = video::bitrate_reconfigure_status_e::invalid;
        return result;
      }
      next_config.rcParams.vbvBufferSize = *scaled_vbv;
    }
    if (baseline_vbv_initial_delay_ != 0) {
      auto scaled_delay = scaled_from_baseline(baseline_vbv_initial_delay_);
      if (!scaled_delay) {
        result.status = video::bitrate_reconfigure_status_e::invalid;
        return result;
      }
      next_config.rcParams.vbvInitialDelay = *scaled_delay;
    }

    auto next_init_params = init_params_;
    next_init_params.encodeConfig = &next_config;

    NV_ENC_RECONFIGURE_PARAMS reconfigure_params = {NV_ENC_RECONFIGURE_PARAMS_VER};
    reconfigure_params.reInitEncodeParams = next_init_params;
    reconfigure_params.resetEncoder = 0;
    reconfigure_params.forceIDR = 0;

    if (apply(&reconfigure_params) != NV_ENC_SUCCESS) {
      result.status = video::bitrate_reconfigure_status_e::failed;
      return result;
    }

    encode_config_ = next_config;
    init_params_ = next_init_params;
    init_params_.encodeConfig = &encode_config_;

    result.status = video::bitrate_reconfigure_status_e::applied;
    result.effective_target_kbps = target_kbps;
    return result;
  }

}  // namespace NVENC_NAMESPACE
