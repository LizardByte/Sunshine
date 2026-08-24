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
   * @brief Encoded NVENC output frame and metadata needed by the packetizer.
   */
  struct nvenc_encoded_frame {
    std::vector<uint8_t> data;  ///< Encoded bitstream bytes returned by NVENC.
    uint64_t frame_index = 0;  ///< Capture-frame index associated with the encoded data.
    bool idr = false;  ///< Whether the encoded frame is an IDR frame.
    bool after_ref_frame_invalidation = false;  ///< Whether the frame follows reference-frame invalidation.
  };

}  // namespace nvenc
