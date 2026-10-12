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
    /**
     * @brief Borrow the discovered endpoints passed to the routing policy.
     * @return Read-only endpoints owned by this fixture.
     */
    const platf::sink_t &discovered_sinks() const {
      return sinks;
    }

    /**
     * @brief Borrow the explicitly configured capture endpoint.
     * @return Configured endpoint whose identity is checked by the tests.
     */
    const std::string &configured_sink() const {
      return configured;
    }

    /**
     * @brief Borrow the empty setting that selects the discovered host endpoint.
     * @return Fixture-owned empty endpoint setting.
     */
    const std::string &unconfigured_sink() const {
      return unconfigured;
    }

    /**
     * @brief Remove the discovered host endpoint for routing fallback checks.
     */
    void clear_host_sink() {
      sinks.host.clear();
    }

    /**
     * @brief Remove all virtual endpoints for missing-device policy checks.
     */
    void remove_virtual_sinks() {
      sinks.null.reset();
    }

    /**
     * @brief Retain the stereo endpoint's identity while clearing its name.
     */
    void clear_virtual_stereo_sink() {
      sinks.null->stereo.clear();
    }

  private:
    platf::sink_t sinks {"host", platf::sink_t::null_t {"stereo", "surround51", "surround71"}};  ///< Discovered endpoints retained for borrowed-name checks.
    const std::string configured {"configured"};  ///< Explicit capture endpoint.
    const std::string unconfigured;  ///< Policy selecting the discovered host endpoint.
  };

  TEST_F(AudioRoutingTest, HostPlaybackUsesExplicitSinkBeforeDiscoveredHost) {
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), configured_sink(), true, 2), &configured_sink());
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), unconfigured_sink(), true, 2), &discovered_sinks().host);
  }

  TEST_F(AudioRoutingTest, DisabledHostPlaybackSelectsMatchingVirtualLayout) {
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), configured_sink(), false, 2), &discovered_sinks().null->stereo);
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), configured_sink(), false, 6), &discovered_sinks().null->surround51);
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), unconfigured_sink(), false, 8), &discovered_sinks().null->surround71);
  }

  TEST_F(AudioRoutingTest, EmptyHostUsesVirtualSinkEvenWhenHostPlaybackIsEnabled) {
    clear_host_sink();
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), unconfigured_sink(), true, 2), &discovered_sinks().null->stereo);
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), configured_sink(), true, 2), &configured_sink());
  }

  TEST_F(AudioRoutingTest, MissingVirtualSinksRetainConfiguredOrHostSelection) {
    remove_virtual_sinks();
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), configured_sink(), false, 2), &configured_sink());
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), unconfigured_sink(), false, 6), &discovered_sinks().host);
    clear_host_sink();
    EXPECT_TRUE(audio::select_capture_sink(discovered_sinks(), unconfigured_sink(), true, 8).empty());
  }

  TEST_F(AudioRoutingTest, UnsupportedLayoutRetainsTheOriginalEndpoint) {
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), configured_sink(), false, 4), &configured_sink());
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), unconfigured_sink(), false, 4), &discovered_sinks().host);
  }

  TEST_F(AudioRoutingTest, EmptyVirtualEndpointPreservesSelectionWithoutFallback) {
    clear_virtual_stereo_sink();
    EXPECT_EQ(&audio::select_capture_sink(discovered_sinks(), configured_sink(), false, 2), &discovered_sinks().null->stereo);
    EXPECT_TRUE(audio::select_capture_sink(discovered_sinks(), configured_sink(), false, 2).empty());
  }
}  // namespace
