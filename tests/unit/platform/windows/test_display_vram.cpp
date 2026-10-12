/**
 * @file tests/unit/platform/windows/test_display_vram.cpp
 * @brief Test src/platform/windows/display_vram.cpp FP16 shader transfer selection.
 */

// test includes
#include "../../../tests_common.h"

#ifdef _WIN32
  // standard includes
  #include <d3dcompiler.h>
  #include <filesystem>
  #include <set>
  #include <tuple>

  // local includes
  #include <src/platform/windows/display.h>

using platf::dxgi::fp16_transfer_e;
using platf::dxgi::select_fp16_transfer;

namespace {
  /**
   * @brief Allocate shader bytecode without requiring a GPU or compiling HLSL.
   * @param value Value identifying the independently compiled resource.
   * @return Owned bytecode blob.
   */
  platf::dxgi::blob_t shader_blob(unsigned char value) {
    ID3DBlob *raw = nullptr;
    if (FAILED(D3DCreateBlob(1, &raw))) {
      return nullptr;
    }
    *static_cast<unsigned char *>(raw->GetBufferPointer()) = value;
    return platf::dxgi::blob_t {raw};
  }
}  // namespace

TEST(ShaderBundleTest, CompilesCompleteImmutableBundleWithCorrectProfiles) {
  std::set<std::string> paths;
  int vertices = 0;
  int pixels = 0;
  const auto bundle = platf::dxgi::compile_shaders([&paths, &vertices, &pixels](const std::string &path, const char *profile) {
    EXPECT_TRUE(paths.insert(path).second);
    EXPECT_TRUE(path.ends_with(".hlsl"));
    if (std::string_view {profile} == "vs_5_0") {
      ++vertices;
    } else {
      EXPECT_EQ(std::string_view {profile}, "ps_5_0");
      ++pixels;
    }
    return shader_blob(1);
  });
  ASSERT_NE(bundle, nullptr);
  EXPECT_EQ(paths.size(), 25);
  EXPECT_EQ(vertices, 6);
  EXPECT_EQ(pixels, 19);
  EXPECT_EQ(bundle->cursor_vs_hlsl->GetBufferSize(), 1);
}

/**
 * @brief Every compiled source and profile reaches the intended shader bundle member.
 */
TEST(ShaderBundleTest, AssignsEachSourceToItsMatchingBytecodeMember) {
  std::vector<std::pair<std::string, std::string>> requests;
  const auto bundle = platf::dxgi::compile_shaders([&requests](const std::string &path, const char *profile) {
    requests.emplace_back(std::filesystem::path {path}.filename().string(), profile);
    return shader_blob(static_cast<unsigned char>(requests.size()));
  });
  ASSERT_NE(bundle, nullptr);
  ASSERT_EQ(requests.size(), 25);
  const auto expect_source = [&requests](const platf::dxgi::shader_bytecode_t &bytecode, const char *source, const char *profile) {
    ASSERT_TRUE(bytecode);
    ASSERT_EQ(bytecode->GetBufferSize(), 1);
    const auto tag = *static_cast<const unsigned char *>(bytecode->GetBufferPointer());
    ASSERT_GT(tag, 0);
    ASSERT_LE(tag, requests.size());
    EXPECT_EQ(requests[tag - 1], (std::pair<std::string, std::string> {source, profile}));
  };
  expect_source(bundle->convert_yuv420_packed_uv_type0_ps_hlsl, "convert_yuv420_packed_uv_type0_ps.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv420_packed_uv_type0_ps_linear_hlsl, "convert_yuv420_packed_uv_type0_ps_linear.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv420_packed_uv_type0_ps_perceptual_quantizer_hlsl, "convert_yuv420_packed_uv_type0_ps_perceptual_quantizer.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv420_packed_uv_type0_vs_hlsl, "convert_yuv420_packed_uv_type0_vs.hlsl", "vs_5_0");
  expect_source(bundle->convert_yuv420_packed_uv_type0s_ps_hlsl, "convert_yuv420_packed_uv_type0s_ps.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv420_packed_uv_type0s_ps_linear_hlsl, "convert_yuv420_packed_uv_type0s_ps_linear.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv420_packed_uv_type0s_ps_perceptual_quantizer_hlsl, "convert_yuv420_packed_uv_type0s_ps_perceptual_quantizer.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv420_packed_uv_type0s_vs_hlsl, "convert_yuv420_packed_uv_type0s_vs.hlsl", "vs_5_0");
  expect_source(bundle->convert_yuv420_planar_y_ps_hlsl, "convert_yuv420_planar_y_ps.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv420_planar_y_ps_linear_hlsl, "convert_yuv420_planar_y_ps_linear.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv420_planar_y_ps_perceptual_quantizer_hlsl, "convert_yuv420_planar_y_ps_perceptual_quantizer.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv420_planar_y_vs_hlsl, "convert_yuv420_planar_y_vs.hlsl", "vs_5_0");
  expect_source(bundle->convert_yuv444_packed_ayuv_ps_hlsl, "convert_yuv444_packed_ayuv_ps.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv444_packed_ayuv_ps_linear_hlsl, "convert_yuv444_packed_ayuv_ps_linear.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv444_packed_vs_hlsl, "convert_yuv444_packed_vs.hlsl", "vs_5_0");
  expect_source(bundle->convert_yuv444_planar_ps_hlsl, "convert_yuv444_planar_ps.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv444_planar_ps_linear_hlsl, "convert_yuv444_planar_ps_linear.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv444_planar_ps_perceptual_quantizer_hlsl, "convert_yuv444_planar_ps_perceptual_quantizer.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv444_packed_y410_ps_hlsl, "convert_yuv444_packed_y410_ps.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv444_packed_y410_ps_linear_hlsl, "convert_yuv444_packed_y410_ps_linear.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv444_packed_y410_ps_perceptual_quantizer_hlsl, "convert_yuv444_packed_y410_ps_perceptual_quantizer.hlsl", "ps_5_0");
  expect_source(bundle->convert_yuv444_planar_vs_hlsl, "convert_yuv444_planar_vs.hlsl", "vs_5_0");
  expect_source(bundle->cursor_ps_hlsl, "cursor_ps.hlsl", "ps_5_0");
  expect_source(bundle->cursor_ps_normalize_white_hlsl, "cursor_ps_normalize_white.hlsl", "ps_5_0");
  expect_source(bundle->cursor_vs_hlsl, "cursor_vs.hlsl", "vs_5_0");
}

/** @brief Compilation failure position in the complete shader set. */
class ShaderFailureTest: public testing::TestWithParam<int> {};

TEST_P(ShaderFailureTest, DoesNotPublishPartialBundleAndCanRetry) {
  int calls = 0;
  EXPECT_EQ(platf::dxgi::compile_shaders([&](const std::string &, const char *) {
              return ++calls == GetParam() ? platf::dxgi::blob_t {nullptr} : shader_blob(1);
            }),
            nullptr);
  EXPECT_EQ(calls, GetParam());
  EXPECT_NE(platf::dxgi::compile_shaders([](const std::string &, const char *) {
              return shader_blob(2);
            }),
            nullptr);
}

INSTANTIATE_TEST_SUITE_P(EveryShader, ShaderFailureTest, testing::Range(1, 26));

TEST(ShaderBundleTest, CaptureRetainsBytecodeAfterPlatformOwnerReleasesIt) {
  config::store_t configuration;
  auto bundle = platf::dxgi::compile_shaders([](const std::string &, const char *) {
    return shader_blob(7);
  });
  auto independent = platf::dxgi::compile_shaders([](const std::string &, const char *) {
    return shader_blob(8);
  });
  std::weak_ptr<const platf::dxgi::shader_bundle_t> weak = bundle;
  auto display = std::make_shared<platf::dxgi::display_ddup_vram_t>(configuration.values(), bundle);
  bundle.reset();
  EXPECT_FALSE(weak.expired());
  EXPECT_EQ(*static_cast<unsigned char *>(display->shaders->cursor_vs_hlsl->GetBufferPointer()), 7);
  EXPECT_EQ(*static_cast<unsigned char *>(independent->cursor_vs_hlsl->GetBufferPointer()), 8);
  display.reset();
  EXPECT_TRUE(weak.expired());
}

TEST(ShaderBundleTest, NativeCompilerBuildsCompleteBundleWithoutCaptureDevice) {
  const auto bundle = platf::dxgi::compile_shaders();
  ASSERT_NE(bundle, nullptr);
  EXPECT_GT(bundle->cursor_ps_hlsl->GetBufferSize(), 0);
  EXPECT_GT(bundle->cursor_vs_hlsl->GetBufferSize(), 0);
}

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
