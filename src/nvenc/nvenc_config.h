#pragma once

namespace nvenc {

  enum class nvenc_two_pass {
    // Single pass, the fastest and no extra vram
    disabled,

    // Larger motion vectors being caught, faster and uses less extra vram
    quarter_resolution,

    // Better overall statistics, slower and uses more extra vram
    full_resolution,
  };

  struct nvenc_config {
    // Quality preset from 1 to 7, higher is slower
    int quality_preset = 1;

    // Use optional preliminary pass for better motion vectors, bitrate distribution and stricter VBV(HRD), uses CUDA cores
    nvenc_two_pass two_pass = nvenc_two_pass::quarter_resolution;

    // Improves fades compression, uses CUDA cores
    bool weighted_prediction = false;

    // Allocate more bitrate to flat regions since they're visually more perceptible, uses CUDA cores
    bool adaptive_quantization = false;

    // Don't use QP below certain value, limits peak image quality to save bitrate
    bool enable_min_qp = false;

    // Min QP value for H.264 when enable_min_qp is selected
    unsigned min_qp_h264 = 19;

    // Min QP value for HEVC when enable_min_qp is selected
    unsigned min_qp_hevc = 23;

    // Min QP value for AV1 when enable_min_qp is selected
    unsigned min_qp_av1 = 23;

    // Use CAVLC entropy coding in H.264 instead of CABAC, not relevant and here for historical reasons
    bool h264_cavlc = false;

    // Add filler data to encoded frames to stay at target bitrate, mainly for testing
    bool insert_filler_data = false;
  };

}  // namespace nvenc
