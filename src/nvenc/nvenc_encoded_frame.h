/**
 * @file src/nvenc/nvenc_encoded_frame.h
 * @brief Declarations for NVENC encoded frame.
 */
#pragma once

// standard includes
#include <cstdint>
#include <vector>

namespace nvenc {

  /**
   * @brief Encoded frame.
   */
  struct nvenc_encoded_frame {
    std::vector<uint8_t> data;
    uint64_t frame_index = 0;
    bool idr = false;
    bool after_ref_frame_invalidation = false;
  };

}  // namespace nvenc
