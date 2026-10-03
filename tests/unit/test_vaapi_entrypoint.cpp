/**
 * @file tests/unit/test_vaapi_entrypoint.cpp
 * @brief Tests for VA-API entrypoint selection without a GPU.
 */
#if defined(__linux__) && defined(SUNSHINE_BUILD_VAAPI)
  #include <gtest/gtest.h>
  #include <src/platform/linux/vaapi_entrypoint.h>

namespace {
  /**
   * @brief Select between LP and normal encoding with mocked capability masks.
   * @param lp Rate-control modes advertised by LP.
   * @param normal Rate-control modes advertised by normal encoding.
   * @param requested Explicit requested mode, or zero for automatic mode.
   * @return Selected entrypoint.
   */
  VAEntrypoint select(uint32_t lp, uint32_t normal, uint32_t requested = 0) {
    const VAEntrypoint eps[] = {VAEntrypointEncSlice, VAEntrypointEncSliceLP};
    return va::select_encoding_entrypoint(eps, requested, [&](VAEntrypoint ep) {
      return ep == VAEntrypointEncSliceLP ? lp : normal;
    });
  }
}  // namespace

TEST(VaapiEntrypoint, AvoidsCqpOnlyLowPowerInAutomaticMode) {
  EXPECT_EQ(VAEntrypointEncSlice, select(VA_RC_CQP, VA_RC_CBR | VA_RC_VBR));
}

TEST(VaapiEntrypoint, KeepsLowPowerWhenBitrateControlIsSupported) {
  EXPECT_EQ(VAEntrypointEncSliceLP, select(VA_RC_CBR, VA_RC_VBR));
  EXPECT_EQ(VAEntrypointEncSliceLP, select(VA_RC_VBR, VA_RC_CBR));
}

TEST(VaapiEntrypoint, HonorsExplicitRateControl) {
  EXPECT_EQ(VAEntrypointEncSliceLP, select(VA_RC_CQP, VA_RC_CBR, VA_RC_CQP));
  EXPECT_EQ(VAEntrypointEncSlice, select(VA_RC_CQP, VA_RC_CBR, VA_RC_CBR));
  EXPECT_EQ(VAEntrypointEncSlice, select(VA_RC_CBR, VA_RC_VBR, VA_RC_VBR));
}

TEST(VaapiEntrypoint, PreservesFallbackWhenNoEntrypointSupportsRequestedMode) {
  EXPECT_EQ(VAEntrypointEncSliceLP, select(VA_RC_CQP, VA_RC_CQP));
  EXPECT_EQ(VAEntrypointEncSliceLP, select(VA_RC_CQP, VA_RC_CBR, VA_RC_ICQ));
}

TEST(VaapiEntrypoint, HandlesFailedAndUnsupportedCapabilityQueries) {
  EXPECT_EQ(VAEntrypointEncSlice, select(0, VA_RC_CBR));
  EXPECT_EQ(VAEntrypointEncSlice, select(VA_ATTRIB_NOT_SUPPORTED, VA_RC_VBR));
  EXPECT_EQ(VAEntrypointEncSliceLP, select(0, VA_ATTRIB_NOT_SUPPORTED));
}

TEST(VaapiEntrypoint, KeepsLowPowerOnlyHardwareWorking) {
  const VAEntrypoint eps[] = {VAEntrypointEncSliceLP};
  EXPECT_EQ(VAEntrypointEncSliceLP, va::select_encoding_entrypoint(eps, 0, [](auto) {
              return VA_RC_CQP;
            }));
}

TEST(VaapiEntrypoint, SupportsPictureEncodingFallback) {
  const VAEntrypoint eps[] = {VAEntrypointVLD, VAEntrypointEncPicture};
  EXPECT_EQ(VAEntrypointEncPicture, va::select_encoding_entrypoint(eps, 0, [](auto) {
              return 0;
            }));
}

TEST(VaapiEntrypoint, ReturnsZeroWithoutAnEncodingEntrypoint) {
  const VAEntrypoint eps[] = {VAEntrypointVLD};
  EXPECT_EQ(0, va::select_encoding_entrypoint(eps, 0, [](auto) {
              return VA_RC_CBR;
            }));
  EXPECT_EQ(0, va::select_encoding_entrypoint(std::span<const VAEntrypoint> {}, 0, [](auto) {
              return VA_RC_CBR;
            }));
}

TEST(VaapiEntrypoint, RespectsAutomaticPolicyWithoutVbrWhitelist) {
  const VAEntrypoint eps[] = {VAEntrypointEncSliceLP, VAEntrypointEncSlice};
  EXPECT_EQ(VAEntrypointEncSliceLP, va::select_encoding_entrypoint(eps, 0, [](auto ep) -> uint32_t {
              return ep == VAEntrypointEncSliceLP ? VA_RC_CQP : VA_RC_VBR;
            },
                                                                   VA_RC_CBR));
}
#endif
