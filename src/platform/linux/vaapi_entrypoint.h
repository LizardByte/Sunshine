/**
 * @file src/platform/linux/vaapi_entrypoint.h
 * @brief Capability-aware VA-API encoding entrypoint selection.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <va/va.h>

namespace va {
  /**
   * @brief Prefer an entrypoint supporting the requested rate control before falling back.
   * @param entrypoints Entrypoints advertised for the selected profile.
   * @param requested_rc Explicit rate-control mask, or zero for automatic bitrate control.
   * @param query_rc Callback returning supported rate-control flags, or zero on query failure.
   * @param automatic_rc Modes accepted by the automatic rate-control policy.
   * @return Selected entrypoint, or zero if no encoding entrypoint is advertised.
   */
  template<class QueryRateControl>
  VAEntrypoint select_encoding_entrypoint(std::span<const VAEntrypoint> entrypoints, uint32_t requested_rc, QueryRateControl query_rc, uint32_t automatic_rc = VA_RC_CBR | VA_RC_VBR) {
    const VAEntrypoint preferences[] = {
      VAEntrypointEncSliceLP,
      VAEntrypointEncSlice,
      VAEntrypointEncPicture
    };
    const auto desired_rc = requested_rc ? requested_rc : automatic_rc;
    auto fallback = static_cast<VAEntrypoint>(0);
    for (auto ep : preferences) {
      if (std::find(entrypoints.begin(), entrypoints.end(), ep) == entrypoints.end()) {
        continue;
      }
      if (!fallback) {
        fallback = ep;
      }
      const uint32_t supported_rc = query_rc(ep);
      if (supported_rc != VA_ATTRIB_NOT_SUPPORTED && (supported_rc & desired_rc)) {
        return ep;
      }
    }
    return fallback;
  }
}  // namespace va
