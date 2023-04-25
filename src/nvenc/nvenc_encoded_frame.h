#pragma once

#include <cstdint>
#include <vector>

namespace nvenc {
  struct nvenc_encoded_frame {
    std::vector<uint8_t> data;
    uint64_t frame_index = 0;
    bool idr = false;
    bool after_ref_frame_invalidation = false;
  };
}  // namespace nvenc
