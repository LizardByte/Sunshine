/**
 * @file src/nvenc/nvenc_d3d11.cpp
 * @brief Definitions for abstract Direct3D11 NVENC encoder.
 */
// local includes
#include "src/logging.h"

#ifdef _WIN32
  #include "nvenc_d3d11.h"

namespace NVENC_NAMESPACE {

  nvenc_d3d11::nvenc_d3d11(NV_ENC_DEVICE_TYPE device_type, ::nvenc::shared_dll dll):
      nvenc_base(device_type),
      dll(std::move(dll)) {
    async_event_handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  }

  nvenc_d3d11::~nvenc_d3d11() {
    if (async_event_handle) {
      CloseHandle(async_event_handle);
    }
  }

  bool nvenc_d3d11::init_library() {
    if (nvenc) {
      return true;
    }

    auto create_instance = reinterpret_cast<decltype(NvEncodeAPICreateInstance) *>(
      GetProcAddress(dll.get(), "NvEncodeAPICreateInstance")
    );
    if (!create_instance) {
      BOOST_LOG(error) << "NvEnc: No NvEncodeAPICreateInstance() in NVENC driver library";
      return false;
    }

    auto new_nvenc = std::make_unique<NV_ENCODE_API_FUNCTION_LIST>();
    new_nvenc->version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (nvenc_failed(create_instance(new_nvenc.get()))) {
      BOOST_LOG(error) << "NvEnc: NvEncodeAPICreateInstance() failed: " << last_nvenc_error_string;
      return false;
    }

    nvenc = std::move(new_nvenc);
    return true;
  }

  bool nvenc_d3d11::wait_for_async_event(uint32_t timeout_ms) {
    return WaitForSingleObject(async_event_handle, timeout_ms) == WAIT_OBJECT_0;
  }

}  // namespace NVENC_NAMESPACE
#endif
