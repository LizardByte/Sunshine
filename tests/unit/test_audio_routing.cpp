/**
 * @file tests/unit/test_audio_routing.cpp
 * @brief Tests for capture sink selection without opening audio devices.
 */

// lib includes
#include <gtest/gtest.h>

// local includes
#include "src/audio.h"

namespace {
  /**
   * @brief Discoverable host and virtual endpoints used by routing policy tests.
   */
  class AudioRoutingTest: public ::testing::Test {
  protected:
    platf::sink_t sinks {"host", platf::sink_t::null_t {"stereo", "surround51", "surround71"}};  ///< Discovered endpoints retained for borrowed-name checks.
    const std::string configured {"configured"};  ///< Explicit capture endpoint.
    const std::string unconfigured;  ///< Policy selecting the discovered host endpoint.
  };

  TEST_F(AudioRoutingTest, HostPlaybackUsesExplicitSinkBeforeDiscoveredHost) {
    EXPECT_EQ(&audio::select_capture_sink(sinks, configured, true, 2), &configured);
    EXPECT_EQ(&audio::select_capture_sink(sinks, unconfigured, true, 2), &sinks.host);
  }

  TEST_F(AudioRoutingTest, DisabledHostPlaybackSelectsMatchingVirtualLayout) {
    EXPECT_EQ(&audio::select_capture_sink(sinks, configured, false, 2), &sinks.null->stereo);
    EXPECT_EQ(&audio::select_capture_sink(sinks, configured, false, 6), &sinks.null->surround51);
    EXPECT_EQ(&audio::select_capture_sink(sinks, unconfigured, false, 8), &sinks.null->surround71);
  }

  TEST_F(AudioRoutingTest, EmptyHostUsesVirtualSinkEvenWhenHostPlaybackIsEnabled) {
    sinks.host.clear();
    EXPECT_EQ(&audio::select_capture_sink(sinks, unconfigured, true, 2), &sinks.null->stereo);
    EXPECT_EQ(&audio::select_capture_sink(sinks, configured, true, 2), &configured);
  }

  TEST_F(AudioRoutingTest, MissingVirtualSinksRetainConfiguredOrHostSelection) {
    sinks.null.reset();
    EXPECT_EQ(&audio::select_capture_sink(sinks, configured, false, 2), &configured);
    EXPECT_EQ(&audio::select_capture_sink(sinks, unconfigured, false, 6), &sinks.host);
    sinks.host.clear();
    EXPECT_TRUE(audio::select_capture_sink(sinks, unconfigured, true, 8).empty());
  }

  TEST_F(AudioRoutingTest, UnsupportedLayoutRetainsTheOriginalEndpoint) {
    EXPECT_EQ(&audio::select_capture_sink(sinks, configured, false, 4), &configured);
    EXPECT_EQ(&audio::select_capture_sink(sinks, unconfigured, false, 4), &sinks.host);
  }

  TEST_F(AudioRoutingTest, EmptyVirtualEndpointPreservesSelectionWithoutFallback) {
    sinks.null->stereo.clear();
    EXPECT_EQ(&audio::select_capture_sink(sinks, configured, false, 2), &sinks.null->stereo);
    EXPECT_TRUE(audio::select_capture_sink(sinks, configured, false, 2).empty());
  }
}  // namespace
