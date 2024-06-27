#pragma once

#include "nvenc_colorspace.h"
#include "nvenc_config.h"
#include "nvenc_encoded_frame.h"

#include "src/stat_trackers.h"
#include "src/video.h"

#include <ffnvcodec/nvEncodeAPI.h>

namespace nvenc {

  class nvenc_base {
  public:
    nvenc_base(NV_ENC_DEVICE_TYPE device_type);
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
    /**
     * @brief Required. Used for loading NvEnc library and setting `nvenc` variable with `NvEncodeAPICreateInstance()`.
     *        Called during `create_encoder()` if `nvenc` variable is not initialized.
     * @return `true` on success, `false` on error
     */
    virtual bool
    init_library() = 0;

    /**
     * @brief Required. Used for creating outside-facing input surface,
     *        registering this surface with `nvenc->nvEncRegisterResource()` and setting `registered_input_buffer` variable.
     *        Called during `create_encoder()`.
     * @return `true` on success, `false` on error
     */
    virtual bool
    create_and_register_input_buffer() = 0;

    /**
     * @brief Optional. Override if you must perform additional operations on the registered input surface in the beginning of `encode_frame()`.
     *        Typically used for interop copy.
     * @return `true` on success, `false` on error
     */
    virtual bool
    synchronize_input_buffer() { return true; }

    /**
     * @brief Optional. Override if you want to create encoder in async mode.
     *        In this case must also set `async_event_handle` variable.
     * @param timeout_ms Wait timeout in milliseconds
     * @return `true` on success, `false` on timeout or error
     */
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

    void *encoder = nullptr;

    struct {
      uint32_t width = 0;
      uint32_t height = 0;
      NV_ENC_BUFFER_FORMAT buffer_format = NV_ENC_BUFFER_FORMAT_UNDEFINED;
      uint32_t ref_frames_in_dpb = 0;
      bool rfi = false;
    } encoder_params;

    std::string last_nvenc_error_string;

    // Derived classes set these variables
    void *device = nullptr;  // set in constructor or init_library()
    std::shared_ptr<NV_ENCODE_API_FUNCTION_LIST> nvenc;  //  set in init_library()
    NV_ENC_REGISTERED_PTR registered_input_buffer = nullptr;  // set in create_and_register_input_buffer()
    void *async_event_handle = nullptr;  // (optional) set in constructor or init_library(), must override wait_for_async_event()

  private:
    NV_ENC_OUTPUT_PTR output_bitstream = nullptr;
    uint32_t minimum_api_version = 0;

    struct {
      uint64_t last_encoded_frame_index = 0;
      bool rfi_needs_confirmation = false;
      std::pair<uint64_t, uint64_t> last_rfi_range;
      stat_trackers::min_max_avg_tracker<float> frame_size_tracker;
    } encoder_state;
  };

}  // namespace nvenc
