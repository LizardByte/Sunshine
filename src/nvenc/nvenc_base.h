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

    const NV_ENC_DEVICE_TYPE device_type;
    void *const device;

    std::unique_ptr<NV_ENCODE_API_FUNCTION_LIST> nvenc;

    void *encoder = nullptr;

    struct {
      uint32_t width = 0;
      uint32_t height = 0;
      NV_ENC_BUFFER_FORMAT buffer_format = NV_ENC_BUFFER_FORMAT_UNDEFINED;
      bool rfi = false;
    } encoder_params;

    // Derived classes set these variables
    NV_ENC_REGISTERED_PTR registered_input_buffer = nullptr;
    void *async_event_handle = nullptr;

    std::string last_error_string;

  private:
    NV_ENC_OUTPUT_PTR output_bitstream = nullptr;

    struct {
      uint64_t last_encoded_frame_index = 0;
      bool rfi_needs_confirmation = false;
      std::pair<uint64_t, uint64_t> last_rfi_range;
      stat_trackers::min_max_avg_tracker<float> frame_size_tracker;
    } encoder_state;
  };

}  // namespace nvenc
