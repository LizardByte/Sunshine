/**
 * @file tests/unit/platform/linux/test_capture_fallback.cpp
 * @brief Test Linux source-selection policy without a compositor or Portal service.
 */
#include "src/platform/linux/capture_fallback.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {
  using platf::capture::fallback_e;

  /** @brief Capture probe responses and externally visible Portal repair requests. */
  struct probes_t {
    bool kwin_available = true;  ///< ScreenCast capability, independent of output count.
    bool portal_available = false;  ///< Portal has an immediately usable capture session.
    std::vector<std::string> events;  ///< Probe order and permitted repair side effects.

    /**
     * @brief Run the production selection function with controlled probe responses.
     * @param requested Original configured capture backend.
     * @param already_selected Whether an earlier backend was selected.
     * @return Additional selected source.
     */
    fallback_e select(std::string_view requested = "", bool already_selected = false) {
      return platf::capture::probe_fallback(
        requested,
        already_selected,
        [this] {
          events.emplace_back("kwin");
          return kwin_available;
        },
        [this](bool allow_repair) {
          events.emplace_back("portal");
          if (!portal_available && allow_repair) {
            events.emplace_back("portal-repair");
          }
          return portal_available;
        }
      );
    }
  };

  TEST(LinuxCaptureFallback, KeepsEveryPreviouslySelectedSourceWithoutProbing) {
    for (const auto option : {"", "kwin", "portal", "kms", "wlr", "x11", "nvfbc"}) {
      probes_t probes;
      EXPECT_EQ(probes.select(option, true), fallback_e::none);
      EXPECT_TRUE(probes.events.empty());
    }
  }

  TEST(LinuxCaptureFallback, ExplicitKwinAcceptsCapabilityWithoutConsultingPortal) {
    probes_t probes;
    probes.portal_available = true;
    EXPECT_EQ(probes.select("kwin"), fallback_e::kwin);
    EXPECT_EQ(probes.events, (std::vector<std::string> {"kwin"}));
  }

  TEST(LinuxCaptureFallback, ExplicitKwinStillFailsWhenProtocolIsUnavailable) {
    probes_t probes;
    probes.kwin_available = false;
    probes.portal_available = true;
    EXPECT_EQ(probes.select("kwin"), fallback_e::none);
    EXPECT_EQ(probes.events, (std::vector<std::string> {"kwin"}));
  }

  TEST(LinuxCaptureFallback, AutomaticKwinSuppressesCompetingPortalRepair) {
    probes_t probes;
    EXPECT_EQ(probes.select(), fallback_e::kwin);
    EXPECT_EQ(probes.events, (std::vector<std::string> {"kwin", "portal"}));
  }

  TEST(LinuxCaptureFallback, WorkingPortalKeepsPriorityOverHeadlessKwin) {
    probes_t probes;
    probes.portal_available = true;
    EXPECT_EQ(probes.select(), fallback_e::portal);
    EXPECT_EQ(probes.events, (std::vector<std::string> {"kwin", "portal"}));
  }

  TEST(LinuxCaptureFallback, WithoutKwinExistingPortalRepairIsPreserved) {
    probes_t probes;
    probes.kwin_available = false;
    EXPECT_EQ(probes.select(), fallback_e::none);
    EXPECT_EQ(probes.events, (std::vector<std::string> {"kwin", "portal", "portal-repair"}));
  }

  TEST(LinuxCaptureFallback, WithoutKwinWorkingPortalIsSelected) {
    probes_t probes;
    probes.kwin_available = false;
    probes.portal_available = true;
    EXPECT_EQ(probes.select(), fallback_e::portal);
  }

  TEST(LinuxCaptureFallback, OtherExplicitOptionsNeverGainHeadlessKwinFallback) {
    // Explicit Portal already owns its permission/fallback policy; do not reinterpret it as automatic.
    for (const auto option : {"portal", "kms", "wlr", "x11", "nvfbc", "unknown"}) {
      probes_t probes;
      EXPECT_EQ(probes.select(option), fallback_e::none);
      EXPECT_TRUE(probes.events.empty());
    }
  }
}  // namespace
