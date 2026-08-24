/**
 * @file src/nvenc/nvenc_config.h
 * @brief Declarations for NVENC encoder configuration.
 */
#pragma once

namespace nvenc {

  /**
   * @brief Enumerates supported nVENC two pass options.
   */
  enum class nvenc_two_pass {
    disabled,  ///< Single pass, the fastest and no extra vram
    quarter_resolution,  ///< Larger motion vectors being caught, faster and uses less extra vram
    full_resolution,  ///< Better overall statistics, slower and uses more extra vram
  };

  /**
   * @brief Enumerates supported nVENC split frame encoding options.
   */
  enum class nvenc_split_frame_encoding {
    disabled,  ///< Disable
    driver_decides,  ///< Let driver decide
    force_enabled,  ///< Force-enable
  };

  /**
   * @brief NVENC encoder configuration.
   */
  struct nvenc_config {
    // Quality preset from 1 to 7, higher is slower
    int quality_preset = 1;  ///< Quality preset.

    // Use optional preliminary pass for better motion vectors, bitrate distribution and stricter VBV(HRD), uses CUDA cores
    nvenc_two_pass two_pass = nvenc_two_pass::quarter_resolution;  ///< Two pass.

    // Percentage increase of VBV/HRD from the default single frame, allows low-latency variable bitrate
    int vbv_percentage_increase = 0;  ///< Vbv percentage increase.

    // Improves fades compression, uses CUDA cores
    bool weighted_prediction = false;  ///< Enable weighted prediction for NVENC.

    // Allocate more bitrate to flat regions since they're visually more perceptible, uses CUDA cores
    bool adaptive_quantization = false;  ///< Enable adaptive quantization for NVENC.

    // Don't use QP below certain value, limits peak image quality to save bitrate
    bool enable_min_qp = false;  ///< Enable minimum QP limits for NVENC.

    // Min QP value for H.264 when enable_min_qp is selected
    unsigned min_qp_h264 = 19;  ///< Min qp h264.

    // Min QP value for HEVC when enable_min_qp is selected
    unsigned min_qp_hevc = 23;  ///< Min qp HEVC.

    // Min QP value for AV1 when enable_min_qp is selected
    unsigned min_qp_av1 = 23;  ///< Min qp AV1.

    // Use CAVLC entropy coding in H.264 instead of CABAC, not relevant and here for historical reasons
    bool h264_cavlc = false;  ///< Use CAVLC entropy coding for H.264.

    // Add filler data to encoded frames to stay at target bitrate, mainly for testing
    bool insert_filler_data = false;  ///< Insert filler data to maintain bitrate constraints.

    // Enable split-frame encoding if the gpu has multiple NVENC hardware clusters
    nvenc_split_frame_encoding split_frame_encoding = nvenc_split_frame_encoding::driver_decides;  ///< Split frame encoding.
  };

}  // namespace nvenc
