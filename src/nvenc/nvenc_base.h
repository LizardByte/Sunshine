/**
 * @file src/nvenc/nvenc_base.h
 * @brief Declarations for base NVENC encoder.
 */
#pragma once

#include "nvenc_colorspace.h"
#include "nvenc_config.h"
#include "nvenc_encoded_frame.h"

#include "src/logging.h"
#include "src/video.h"

#include <ffnvcodec/nvEncodeAPI.h>

namespace nvenc {

  class nvenc_base {
  public:
    nvenc_base(NV_ENC_DEVICE_TYPE device_type, void *device);
    virtual ~nvenc_base();

    nvenc_base(const nvenc_base &) = delete;
    nvenc_base &
    operator=(const nvenc_base &) = delete;

    bool
    create_encoder(const nvenc_config &config, const video::config_t &client_config, const nvenc_colorspace_t &colorspace, NV_ENC_BUFFER_FORMAT buffer_format);

    void
    destroy_encoder();

    nvenc_encoded_frame
    encode_frame(uint64_t frame_index, bool force_idr);

    bool
    invalidate_ref_frames(uint64_t first_frame, uint64_t last_frame);

  protected:
    virtual bool
    init_library() = 0;

    virtual bool
    create_and_register_input_buffer() = 0;

    virtual bool
    wait_for_async_event(uint32_t timeout_ms) { return false; }

    bool
    nvenc_failed(NVENCSTATUS status);

    /**
     * @brief This function returns the corresponding struct version for the minimum API required by the codec.
     * @details Reducing the struct versions maximizes driver compatibility by avoiding needless API breaks.
     * @param version The raw structure version from `NVENCAPI_STRUCT_VERSION()`.
     * @param v11_struct_version Optionally specifies the struct version to use with v11 SDK major versions.
     * @param v12_struct_version Optionally specifies the struct version to use with v12 SDK major versions.
     * @return A suitable struct version for the active codec.
     */
    uint32_t
    min_struct_version(uint32_t version, uint32_t v11_struct_version = 0, uint32_t v12_struct_version = 0);

    const NV_ENC_DEVICE_TYPE device_type;
    void *const device;

    std::unique_ptr<NV_ENCODE_API_FUNCTION_LIST> nvenc;

    void *encoder = nullptr;

    struct {
      uint32_t width = 0;
      uint32_t height = 0;
      NV_ENC_BUFFER_FORMAT buffer_format = NV_ENC_BUFFER_FORMAT_UNDEFINED;
      uint32_t ref_frames_in_dpb = 0;
      bool rfi = false;
    } encoder_params;

    // Derived classes set these variables
    NV_ENC_REGISTERED_PTR registered_input_buffer = nullptr;
    void *async_event_handle = nullptr;

    std::string last_error_string;

  private:
    NV_ENC_OUTPUT_PTR output_bitstream = nullptr;
    uint32_t minimum_api_version = 0;

    struct {
      uint64_t last_encoded_frame_index = 0;
      bool rfi_needs_confirmation = false;
      std::pair<uint64_t, uint64_t> last_rfi_range;
      logging::min_max_avg_periodic_logger<double> frame_size_logger = { debug, "NvEnc: encoded frame sizes in kB", "" };
    } encoder_state;
  };

}  // namespace nvenc
