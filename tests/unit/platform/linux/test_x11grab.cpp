/**
 * @file tests/unit/platform/linux/test_x11grab.cpp
 * @brief Tests for X11 capture setup when RandR queries fail.
 */

#ifdef SUNSHINE_BUILD_X11

  // standard includes
  #include <memory>
  #include <string>
  #include <vector>

  // test includes
  #include "../../../tests_common.h"

  // local includes
  #include "src/platform/linux/x11grab.h"
  #include "src/video.h"

namespace platf {
  std::vector<std::string> x11_display_names();
  std::shared_ptr<display_t> x11_display(mem_type_e hwdevice_type, const std::string &display_name, const ::video::config_t &config);
}  // namespace platf

namespace {
  const video::config_t capture_config {.width = 1920, .height = 1080, .framerate = 60};

  struct X11GrabTest: PlatformTestSuite {
    void SetUp() override {
      PlatformTestSuite::SetUp();
      if (platf::x11_display_names().empty()) {
        GTEST_SKIP() << "No X11 display with RandR outputs is available";
      }
    }

    void TearDown() override {
      ASSERT_EQ(platf::x11::fail_randr_for_testing(0, false), 0);
      PlatformTestSuite::TearDown();
    }
  };
}  // namespace

TEST_F(X11GrabTest, DisplayNamesRetryTransientScreenResourcesFailure) {
  ASSERT_EQ(platf::x11::fail_randr_for_testing(2, false), 0);
  EXPECT_FALSE(platf::x11_display_names().empty());
}

TEST_F(X11GrabTest, DisplayNamesEmptyWhenScreenResourcesKeepFailing) {
  ASSERT_EQ(platf::x11::fail_randr_for_testing(-1, false), 0);
  EXPECT_TRUE(platf::x11_display_names().empty());
}

TEST_F(X11GrabTest, DisplaySucceedsForFirstMonitor) {
  EXPECT_TRUE(platf::x11_display(platf::mem_type_e::system, "0", capture_config));
}

TEST_F(X11GrabTest, DisplayFailsWhenScreenResourcesUnavailable) {
  ASSERT_EQ(platf::x11::fail_randr_for_testing(-1, false), 0);
  EXPECT_FALSE(platf::x11_display(platf::mem_type_e::system, "0", capture_config));
}

TEST_F(X11GrabTest, DisplayFailsWhenCrtcInfoUnavailable) {
  ASSERT_EQ(platf::x11::fail_randr_for_testing(0, true), 0);
  EXPECT_FALSE(platf::x11_display(platf::mem_type_e::system, "0", capture_config));
}

#endif
