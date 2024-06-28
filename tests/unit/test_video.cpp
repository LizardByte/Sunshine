/**
 * @file tests/unit/test_video.cpp
 * @brief Test src/video.*.
 */
#include <src/video.h>

#include <tests/conftest.cpp>

class EncoderTest: public virtual BaseTest, public PlatformInitBase, public ::testing::WithParamInterface<std::tuple<std::basic_string_view<char>, video::encoder_t *>> {
protected:
  void
  SetUp() override {
    BaseTest::SetUp();
    PlatformInitBase::SetUp();

    std::string_view p_name = std::get<0>(GetParam());
    std::cout << "EncoderTest(" << p_name << "):: starting Fixture SetUp" << std::endl;

    std::cout << "EncoderTest(" << p_name << "):: validating encoder" << std::endl;
    video::encoder_t *encoder = std::get<1>(GetParam());
    bool isEncoderValid;
    isEncoderValid = video::validate_encoder(*encoder, false);

    if (!isEncoderValid) {
      // if encoder is software fail, otherwise skip
      if (encoder == &video::software && std::string(TESTS_SOFTWARE_ENCODER_UNAVAILABLE) == "fail") {
        FAIL() << "EncoderTest(" << p_name << "):: software encoder not available";
      }
      else {
        GTEST_SKIP_((std::string("EncoderTest(") + std::string(p_name) + "):: encoder not available").c_str());
      }
    }
    else {
      std::cout << "EncoderTest(" << p_name << "):: encoder available" << std::endl;
    }
  }

  void
  TearDown() override {
    PlatformInitBase::TearDown();
    BaseTest::TearDown();
  }
};
INSTANTIATE_TEST_SUITE_P(
  EncoderVariants,
  EncoderTest,
  ::testing::Values(
#if !defined(__APPLE__)
    std::make_tuple(video::nvenc.name, &video::nvenc),
#endif
#ifdef _WIN32
    std::make_tuple(video::amdvce.name, &video::amdvce), std::make_tuple(video::quicksync.name, &video::quicksync),
#endif
#ifdef __linux__
    std::make_tuple(video::vaapi.name, &video::vaapi),
#endif
#ifdef __APPLE__
    std::make_tuple(video::videotoolbox.name, &video::videotoolbox),
#endif
    std::make_tuple(video::software.name, &video::software)));
TEST_P(EncoderTest, ValidateEncoder) {
  // todo:: test something besides fixture setup
}
