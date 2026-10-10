/**
 * @file tests/unit/platform/windows/test_display_vram.cpp
 * @brief Test src/platform/windows/display_vram.cpp FP16 shader transfer selection.
 */

// test includes
#include "../../../tests_common.h"

#ifdef _WIN32
  // standard includes
  #include <tuple>

  // local includes
  #include <src/platform/windows/display.h>

using platf::dxgi::fp16_transfer_e;
using platf::dxgi::select_fp16_transfer;

/**
 * @brief Encoder output format, HDR state, option state, and expected FP16 transfer.
 */
using Fp16TransferParam = std::tuple<DXGI_FORMAT, bool, bool, fp16_transfer_e>;

/**
 * @brief Parameterized coverage for every output format with HDR and the opt-in option toggled.
 */
struct Fp16TransferTest: testing::TestWithParam<Fp16TransferParam> {};

TEST_P(Fp16TransferTest, SelectsExpectedTransfer) {
  const auto &[format, hdr, gamma_encoded_sdr, expected] = GetParam();

  EXPECT_EQ(expected, select_fp16_transfer(format, hdr, gamma_encoded_sdr));
}

INSTANTIATE_TEST_SUITE_P(
  LinearSdrByDefault,
  Fp16TransferTest,
  testing::Values(
    Fp16TransferParam {DXGI_FORMAT_NV12, false, false, fp16_transfer_e::linear},
    Fp16TransferParam {DXGI_FORMAT_P010, false, false, fp16_transfer_e::linear},
    Fp16TransferParam {DXGI_FORMAT_R16_UINT, false, false, fp16_transfer_e::linear},
    Fp16TransferParam {DXGI_FORMAT_AYUV, false, false, fp16_transfer_e::linear},
    Fp16TransferParam {DXGI_FORMAT_Y410, false, false, fp16_transfer_e::linear}
  )
);

INSTANTIATE_TEST_SUITE_P(
  GammaEncodedSdrWhenEnabled,
  Fp16TransferTest,
  testing::Values(
    Fp16TransferParam {DXGI_FORMAT_NV12, false, true, fp16_transfer_e::gamma_encoded},
    Fp16TransferParam {DXGI_FORMAT_P010, false, true, fp16_transfer_e::gamma_encoded},
    Fp16TransferParam {DXGI_FORMAT_R16_UINT, false, true, fp16_transfer_e::gamma_encoded},
    Fp16TransferParam {DXGI_FORMAT_AYUV, false, true, fp16_transfer_e::gamma_encoded},
    Fp16TransferParam {DXGI_FORMAT_Y410, false, true, fp16_transfer_e::gamma_encoded}
  )
);

INSTANTIATE_TEST_SUITE_P(
  HdrUnaffectedByOption,
  Fp16TransferTest,
  testing::Values(
    Fp16TransferParam {DXGI_FORMAT_NV12, true, false, fp16_transfer_e::linear},
    Fp16TransferParam {DXGI_FORMAT_NV12, true, true, fp16_transfer_e::linear},
    Fp16TransferParam {DXGI_FORMAT_P010, true, false, fp16_transfer_e::perceptual_quantizer},
    Fp16TransferParam {DXGI_FORMAT_P010, true, true, fp16_transfer_e::perceptual_quantizer},
    Fp16TransferParam {DXGI_FORMAT_R16_UINT, true, false, fp16_transfer_e::perceptual_quantizer},
    Fp16TransferParam {DXGI_FORMAT_R16_UINT, true, true, fp16_transfer_e::perceptual_quantizer},
    Fp16TransferParam {DXGI_FORMAT_AYUV, true, false, fp16_transfer_e::linear},
    Fp16TransferParam {DXGI_FORMAT_AYUV, true, true, fp16_transfer_e::linear},
    Fp16TransferParam {DXGI_FORMAT_Y410, true, false, fp16_transfer_e::perceptual_quantizer},
    Fp16TransferParam {DXGI_FORMAT_Y410, true, true, fp16_transfer_e::perceptual_quantizer}
  )
);
#endif
