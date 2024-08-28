/**
 * @file tests/unit/test_video.cpp
 * @brief Test src/video.*.
 */
#include <src/video.h>

#include "../tests_common.h"

struct EncoderTest: PlatformTestSuite, testing::WithParamInterface<video::encoder_t *> {
  void
  SetUp() override {
    auto &encoder = *GetParam();
    if (!video::validate_encoder(encoder, false)) {
      // Encoder failed validation,
      // if it's software - fail (unless overriden with compile definition), otherwise skip
      if (encoder.name == "software" && std::string(TESTS_SOFTWARE_ENCODER_UNAVAILABLE) == "fail") {
        FAIL() << "Software encoder not available";
      }
      else {
        GTEST_SKIP() << "Encoder not available";
      }
    }
  }
};

INSTANTIATE_TEST_SUITE_P(
  EncoderVariants,
  EncoderTest,
  testing::Values(
#if !defined(__APPLE__)
    &video::nvenc,
#endif
#ifdef _WIN32
    &video::amdvce,
    &video::quicksync,
#endif
#ifdef __linux__
    &video::vaapi,
#endif
#ifdef __APPLE__
    &video::videotoolbox,
#endif
    &video::software),
  [](const auto &info) { return std::string(info.param->name); });

TEST_P(EncoderTest, ValidateEncoder) {
  // todo:: test something besides fixture setup
}
