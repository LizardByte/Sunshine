/**
 * @file tests/unit/test_nvenc_reconfigure.cpp
 * @brief Test atomic NVENC bitrate reconfiguration state.
 */
#include "../tests_common.h"

#ifdef _WIN32

#include <limits>

#define NVENC_NAMESPACE nvenc_1300
#define NVENC_SDK_VERSION 1300
#include <src/nvenc/nvenc_reconfigure.h>

using namespace nvenc_1300;

namespace {

  struct nvenc_reconfigure_fixture_t {
    NV_ENC_INITIALIZE_PARAMS init_params = {NV_ENC_INITIALIZE_PARAMS_VER};
    NV_ENC_CONFIG encode_config = {NV_ENC_CONFIG_VER};

    nvenc_reconfigure_fixture_t() {
      encode_config.gopLength = 777;
      encode_config.rcParams.averageBitRate = 25'000'000;
      encode_config.rcParams.maxBitRate = 25'000'000;
      encode_config.rcParams.vbvBufferSize = 500'000;
      encode_config.rcParams.vbvInitialDelay = 250'000;
      init_params.encodeConfig = &encode_config;
      init_params.encodeWidth = 1920;
      init_params.encodeHeight = 1080;
    }
  };

}  // namespace

TEST(NvencBitrateReconfigureTest, ReportsUnsupportedWithoutCallingDriver) {
  nvenc_reconfigure_fixture_t fixture;
  nvenc_1300::bitrate_reconfigure_state_t state;
  state.initialize(false, fixture.init_params, fixture.encode_config);
  bool called = false;

  const auto result = state.reconfigure(40'000, [&](auto *) {
    called = true;
    return NV_ENC_SUCCESS;
  });

  EXPECT_EQ(video::bitrate_reconfigure_status_e::unsupported, result.status);
  EXPECT_EQ(25'000U, result.old_target_kbps);
  EXPECT_EQ(40'000U, result.requested_target_kbps);
  EXPECT_EQ(25'000U, result.effective_target_kbps);
  EXPECT_FALSE(called);
}

TEST(NvencBitrateReconfigureTest, RejectsInvalidAndUnchangedTargetsWithoutCallingDriver) {
  nvenc_reconfigure_fixture_t fixture;
  nvenc_1300::bitrate_reconfigure_state_t state;
  state.initialize(true, fixture.init_params, fixture.encode_config);
  bool called = false;
  auto apply = [&](auto *) {
    called = true;
    return NV_ENC_SUCCESS;
  };

  EXPECT_EQ(video::bitrate_reconfigure_status_e::invalid, state.reconfigure(0, apply).status);
  EXPECT_EQ(
    video::bitrate_reconfigure_status_e::invalid,
    state.reconfigure(std::numeric_limits<std::uint32_t>::max(), apply).status
  );
  EXPECT_EQ(video::bitrate_reconfigure_status_e::unchanged, state.reconfigure(25'000, apply).status);
  EXPECT_FALSE(called);
}

TEST(NvencBitrateReconfigureTest, AppliesAtomicRateControlValuesWithoutResetOrIdr) {
  nvenc_reconfigure_fixture_t fixture;
  nvenc_1300::bitrate_reconfigure_state_t state;
  state.initialize(true, fixture.init_params, fixture.encode_config);

  const auto result = state.reconfigure(40'000, [&](auto *params) -> NVENCSTATUS {
    EXPECT_NE(nullptr, params);
    if (!params) {
      return NV_ENC_ERR_INVALID_PTR;
    }
    EXPECT_NE(nullptr, params->reInitEncodeParams.encodeConfig);
    if (!params->reInitEncodeParams.encodeConfig) {
      return NV_ENC_ERR_INVALID_PTR;
    }
    EXPECT_EQ(40'000'000U, params->reInitEncodeParams.encodeConfig->rcParams.averageBitRate);
    EXPECT_EQ(40'000'000U, params->reInitEncodeParams.encodeConfig->rcParams.maxBitRate);
    EXPECT_EQ(800'000U, params->reInitEncodeParams.encodeConfig->rcParams.vbvBufferSize);
    EXPECT_EQ(400'000U, params->reInitEncodeParams.encodeConfig->rcParams.vbvInitialDelay);
    EXPECT_EQ(777U, params->reInitEncodeParams.encodeConfig->gopLength);
    EXPECT_EQ(1920U, params->reInitEncodeParams.encodeWidth);
    EXPECT_EQ(1080U, params->reInitEncodeParams.encodeHeight);
    EXPECT_EQ(0U, params->resetEncoder);
    EXPECT_EQ(0U, params->forceIDR);
    return NV_ENC_SUCCESS;
  });

  EXPECT_EQ(video::bitrate_reconfigure_status_e::applied, result.status);
  EXPECT_EQ(25'000U, result.old_target_kbps);
  EXPECT_EQ(40'000U, result.effective_target_kbps);
}

TEST(NvencBitrateReconfigureTest, ScalesFromBaselineWithoutCumulativeDrift) {
  nvenc_reconfigure_fixture_t fixture;
  nvenc_1300::bitrate_reconfigure_state_t state;
  state.initialize(true, fixture.init_params, fixture.encode_config);

  EXPECT_EQ(
    video::bitrate_reconfigure_status_e::applied,
    state.reconfigure(40'000, [](auto *) {
           return NV_ENC_SUCCESS;
         })
      .status
  );

  const auto result = state.reconfigure(25'000, [&](auto *params) {
    EXPECT_EQ(25'000'000U, params->reInitEncodeParams.encodeConfig->rcParams.averageBitRate);
    EXPECT_EQ(25'000'000U, params->reInitEncodeParams.encodeConfig->rcParams.maxBitRate);
    EXPECT_EQ(500'000U, params->reInitEncodeParams.encodeConfig->rcParams.vbvBufferSize);
    EXPECT_EQ(250'000U, params->reInitEncodeParams.encodeConfig->rcParams.vbvInitialDelay);
    return NV_ENC_SUCCESS;
  });

  EXPECT_EQ(video::bitrate_reconfigure_status_e::applied, result.status);
  EXPECT_EQ(40'000U, result.old_target_kbps);
  EXPECT_EQ(25'000U, result.effective_target_kbps);
}

TEST(NvencBitrateReconfigureTest, KeepsLastSuccessfulStateWhenDriverFails) {
  nvenc_reconfigure_fixture_t fixture;
  nvenc_1300::bitrate_reconfigure_state_t state;
  state.initialize(true, fixture.init_params, fixture.encode_config);

  const auto failed = state.reconfigure(40'000, [](auto *) {
    return NV_ENC_ERR_INVALID_PARAM;
  });
  EXPECT_EQ(video::bitrate_reconfigure_status_e::failed, failed.status);
  EXPECT_EQ(25'000U, failed.effective_target_kbps);

  const auto retry = state.reconfigure(35'000, [](auto *) {
    return NV_ENC_SUCCESS;
  });
  EXPECT_EQ(video::bitrate_reconfigure_status_e::applied, retry.status);
  EXPECT_EQ(25'000U, retry.old_target_kbps);
  EXPECT_EQ(35'000U, retry.effective_target_kbps);
}

TEST(NvencBitrateReconfigureTest, RejectsMissingBaselineOrDriverCallback) {
  nvenc_reconfigure_fixture_t fixture;
  nvenc_1300::bitrate_reconfigure_state_t state;
  fixture.encode_config.rcParams.averageBitRate = 0;
  state.initialize(true, fixture.init_params, fixture.encode_config);

  EXPECT_EQ(
    video::bitrate_reconfigure_status_e::failed,
    state.reconfigure(40'000, [](auto *) {
           return NV_ENC_SUCCESS;
         })
      .status
  );

  fixture.encode_config.rcParams.averageBitRate = 25'000'000;
  state.initialize(true, fixture.init_params, fixture.encode_config);
  EXPECT_EQ(
    video::bitrate_reconfigure_status_e::failed,
    state.reconfigure(40'000, {}).status
  );

  state.reset();
  EXPECT_EQ(
    video::bitrate_reconfigure_status_e::unsupported,
    state.reconfigure(40'000, [](auto *) {
           return NV_ENC_SUCCESS;
         })
      .status
  );
}

TEST(NvencBitrateReconfigureTest, LeavesDefaultVbvFieldsUnset) {
  nvenc_reconfigure_fixture_t fixture;
  nvenc_1300::bitrate_reconfigure_state_t state;
  fixture.encode_config.rcParams.vbvBufferSize = 0;
  fixture.encode_config.rcParams.vbvInitialDelay = 0;
  state.initialize(true, fixture.init_params, fixture.encode_config);

  const auto result = state.reconfigure(40'000, [](auto *params) {
    EXPECT_EQ(0U, params->reInitEncodeParams.encodeConfig->rcParams.vbvBufferSize);
    EXPECT_EQ(0U, params->reInitEncodeParams.encodeConfig->rcParams.vbvInitialDelay);
    return NV_ENC_SUCCESS;
  });

  EXPECT_EQ(video::bitrate_reconfigure_status_e::applied, result.status);
}

TEST(NvencBitrateReconfigureTest, RejectsVbvScalingOverflowWithoutCallingDriver) {
  nvenc_reconfigure_fixture_t fixture;
  nvenc_1300::bitrate_reconfigure_state_t state;
  fixture.encode_config.rcParams.averageBitRate = 1;
  fixture.encode_config.rcParams.vbvBufferSize = std::numeric_limits<std::uint32_t>::max();
  state.initialize(true, fixture.init_params, fixture.encode_config);
  bool called = false;

  const auto result = state.reconfigure(4'294'967, [&](auto *) {
    called = true;
    return NV_ENC_SUCCESS;
  });

  EXPECT_EQ(video::bitrate_reconfigure_status_e::invalid, result.status);
  EXPECT_FALSE(called);
}

TEST(NvencBitrateReconfigureTest, RejectsVbvDelayScalingOverflowWithoutCallingDriver) {
  nvenc_reconfigure_fixture_t fixture;
  nvenc_1300::bitrate_reconfigure_state_t state;
  fixture.encode_config.rcParams.averageBitRate = 1;
  fixture.encode_config.rcParams.vbvBufferSize = 0;
  fixture.encode_config.rcParams.vbvInitialDelay = std::numeric_limits<std::uint32_t>::max();
  state.initialize(true, fixture.init_params, fixture.encode_config);
  bool called = false;

  const auto result = state.reconfigure(4'294'967, [&](auto *) {
    called = true;
    return NV_ENC_SUCCESS;
  });

  EXPECT_EQ(video::bitrate_reconfigure_status_e::invalid, result.status);
  EXPECT_FALSE(called);
}

#endif  // _WIN32
