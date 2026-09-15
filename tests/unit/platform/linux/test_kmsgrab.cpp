/**
 * @file tests/unit/platform/linux/test_kmsgrab.cpp
 * @brief Tests for portable KMS monitor descriptor helpers.
 */
#include "../../../tests_common.h"

// standard includes
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// local includes
#include "src/platform/linux/kmsgrab.h"

namespace {

  /**
   * @brief Input and expected output for KMS viewport resolution tests.
   */
  struct monitor_viewport_test_case_t {
    std::string name;  ///< Descriptive test-case name.
    std::vector<platf::kms::card_descriptor_t> card_descriptors;  ///< Cached card descriptors.
    std::string card_path;  ///< Live DRM card filename.
    std::uint32_t crtc_id;  ///< Live DRM CRTC identifier.
    platf::touch_port_t live_crtc_viewport;  ///< Fallback live CRTC geometry.
    platf::touch_port_t expected_viewport;  ///< Expected resolved geometry.
    platf::kms::monitor_viewport_source_e expected_source;  ///< Expected geometry source.
  };

  /**
   * @brief Parameterized fixture for KMS viewport resolution.
   */
  class KmsMonitorViewportTest: public testing::TestWithParam<monitor_viewport_test_case_t> {};

  TEST_P(KmsMonitorViewportTest, ResolvesCachedOrLiveCrtcGeometry) {
    const auto &test_case = GetParam();
    const auto result = platf::kms::resolve_monitor_viewport(
      test_case.card_descriptors,
      test_case.card_path,
      test_case.crtc_id,
      test_case.live_crtc_viewport
    );

    EXPECT_EQ(result.source, test_case.expected_source);
    EXPECT_EQ(result.viewport.offset_x, test_case.expected_viewport.offset_x);
    EXPECT_EQ(result.viewport.offset_y, test_case.expected_viewport.offset_y);
    EXPECT_EQ(result.viewport.width, test_case.expected_viewport.width);
    EXPECT_EQ(result.viewport.height, test_case.expected_viewport.height);
    EXPECT_EQ(result.viewport.logical_width, test_case.expected_viewport.logical_width);
    EXPECT_EQ(result.viewport.logical_height, test_case.expected_viewport.logical_height);
  }

  INSTANTIATE_TEST_SUITE_P(
    KmsMonitorViewportCases,
    KmsMonitorViewportTest,
    testing::Values(
      monitor_viewport_test_case_t {
        "cached monitor",
        {{"card2", {{42, {10, 1, 0, {100, 200, 2560, 1600, 1280, 800}}}}}},
        "card2",
        42,
        {0, 0, 1920, 1080, 1920, 1080},
        {100, 200, 2560, 1600, 1280, 800},
        platf::kms::monitor_viewport_source_e::cached,
      },
      monitor_viewport_test_case_t {
        "missing secondary card",
        {{"card1", {{17, {11, 1, 0, {0, 0, 1920, 1080, 1920, 1080}}}}}},
        "card2",
        42,
        {0, 0, 2560, 1600, 2560, 1600},
        {0, 0, 2560, 1600, 2560, 1600},
        platf::kms::monitor_viewport_source_e::live_crtc_missing_card,
      },
      monitor_viewport_test_case_t {
        "missing monitor on cached card",
        {{"card2", {{17, {10, 1, 0, {0, 0, 1920, 1080, 1920, 1080}}}}}},
        "card2",
        42,
        {300, 0, 2560, 1600, 2560, 1600},
        {300, 0, 2560, 1600, 2560, 1600},
        platf::kms::monitor_viewport_source_e::live_crtc_missing_monitor,
      }
    ),
    [](const testing::TestParamInfo<monitor_viewport_test_case_t> &info) {
      std::string name = info.param.name;
      std::ranges::replace(name, ' ', '_');
      return name;
    }
  );

}  // namespace
