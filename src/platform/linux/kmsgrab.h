/**
 * @file src/platform/linux/kmsgrab.h
 * @brief Portable KMS monitor descriptor and viewport helpers.
 */
#pragma once

// standard includes
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// local includes
#include "src/platform/common.h"

namespace platf::kms {

  /**
   * @brief KMS monitor capture state and DRM resources.
   */
  struct monitor_t {
    // Connector attributes
    std::uint32_t type;  ///< Type.
    std::uint32_t index;  ///< Index.

    // Monitor index in the global list
    std::uint32_t monitor_index;  ///< Monitor index.

    platf::touch_port_t viewport;  ///< Viewport.
  };

  /**
   * @brief DRM card, device path, and monitor metadata.
   */
  struct card_descriptor_t {
    /**
     * @brief Construct a DRM card descriptor.
     *
     * @param card_path DRM card filename.
     * @param monitors CRTC-to-monitor lookup.
     */
    card_descriptor_t(std::string card_path, std::map<std::uint32_t, monitor_t> monitors):
        path {std::move(card_path)},
        crtc_to_monitor {std::move(monitors)} {
    }

    /**
     * @brief Copy a DRM card descriptor.
     *
     * @param other Descriptor to copy.
     */
    card_descriptor_t(const card_descriptor_t &other) = default;

    /**
     * @brief Move a DRM card descriptor without throwing.
     *
     * @param other Descriptor to move.
     */
    card_descriptor_t(card_descriptor_t &&other) noexcept = default;

    /**
     * @brief Copy-assign a DRM card descriptor.
     *
     * @param other Descriptor to copy.
     * @return Reference to this descriptor.
     */
    card_descriptor_t &operator=(const card_descriptor_t &other) = default;

    /**
     * @brief Move-assign a DRM card descriptor without throwing.
     *
     * @param other Descriptor to move.
     * @return Reference to this descriptor.
     */
    card_descriptor_t &operator=(card_descriptor_t &&other) noexcept = default;

    std::string path;  ///< DRM card filename.
    std::map<std::uint32_t, monitor_t> crtc_to_monitor;  ///< CRTC-to-monitor lookup.
  };

  /**
   * @brief Source used to resolve a KMS monitor viewport.
   */
  enum class monitor_viewport_source_e {
    cached,  ///< Viewport came from the cached card and monitor descriptor.
    live_crtc_missing_card,  ///< Cached card was absent, so live CRTC geometry was used.
    live_crtc_missing_monitor,  ///< Cached CRTC was absent, so live CRTC geometry was used.
  };

  /**
   * @brief Resolved KMS viewport and its source.
   */
  struct monitor_viewport_result_t {
    platf::touch_port_t viewport;  ///< Resolved viewport.
    monitor_viewport_source_e source;  ///< Source of the resolved viewport.
  };

  /**
   * @brief Resolve cached monitor geometry, falling back to the live CRTC.
   *
   * @param card_descriptors Cached KMS card descriptors from display enumeration.
   * @param card_path Filename of the live DRM card being captured.
   * @param crtc_id Live DRM CRTC identifier being captured.
   * @param live_crtc_viewport Viewport derived from the live CRTC.
   * @return Cached monitor viewport when available; otherwise the live CRTC viewport.
   */
  inline monitor_viewport_result_t resolve_monitor_viewport(
    const std::vector<card_descriptor_t> &card_descriptors,
    const std::string_view card_path,
    const std::uint32_t crtc_id,
    const platf::touch_port_t &live_crtc_viewport
  ) {
    using enum monitor_viewport_source_e;

    const auto card = std::ranges::find(card_descriptors, card_path, &card_descriptor_t::path);
    if (card == std::end(card_descriptors)) {
      return {live_crtc_viewport, live_crtc_missing_card};
    }

    const auto monitor = card->crtc_to_monitor.find(crtc_id);
    if (monitor == std::end(card->crtc_to_monitor)) {
      return {live_crtc_viewport, live_crtc_missing_monitor};
    }

    return {monitor->second.viewport, cached};
  }

}  // namespace platf::kms
