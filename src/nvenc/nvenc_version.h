/**
 * @file src/nvenc/nvenc_version.h
 * @brief NVENC SDK version selection helpers.
 */
#pragma once

// standard includes
#include <cstdint>
#include <utility>

namespace nvenc {

  /**
   * @brief NVENC SDK implementations compiled into Sunshine.
   */
  enum class nvenc_sdk_version : std::uint32_t {
    unsupported = 0U,  ///< No compatible SDK implementation is available.
    sdk_11_0 = 1100U,  ///< Video Codec SDK 11.0.
    sdk_12_0 = 1200U,  ///< Video Codec SDK 12.0.
    sdk_13_0 = 1300U,  ///< Video Codec SDK 13.0.
  };

  /**
   * @brief Convert the packed driver API version to a comparable integer.
   *
   * @param version Version returned by `NvEncodeAPIGetMaxSupportedVersion()`.
   * @return Version encoded as `major * 100 + minor`.
   */
  constexpr std::uint32_t decode_nvenc_driver_version(std::uint32_t version) {
    return (version >> 4U) * 100U + (version & 0x0FU);
  }

  /**
   * @brief Select the newest compiled SDK supported by the installed driver.
   *
   * @param max_version Maximum driver API version encoded as `major * 100 + minor`.
   * @return Selected SDK implementation, or `unsupported` when the driver is too old.
   */
  constexpr nvenc_sdk_version select_nvenc_sdk_version(std::uint32_t max_version) {
    using enum nvenc_sdk_version;
    if (max_version >= std::to_underlying(sdk_13_0)) {
      return sdk_13_0;
    }
    if (max_version >= std::to_underlying(sdk_12_0)) {
      return sdk_12_0;
    }
    if (max_version >= std::to_underlying(sdk_11_0)) {
      return sdk_11_0;
    }
    return unsupported;
  }

}  // namespace nvenc
