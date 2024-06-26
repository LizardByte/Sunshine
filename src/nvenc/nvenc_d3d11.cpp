/**
 * @file src/nvenc/nvenc_d3d11.cpp
 * @brief Definitions for base NVENC d3d11.
 */
#include "src/logging.h"

#ifdef _WIN32
  #include "nvenc_d3d11.h"

  #include "nvenc_utils.h"

namespace nvenc {

  nvenc_d3d11::nvenc_d3d11(ID3D11Device *d3d_device):
      nvenc_base(NV_ENC_DEVICE_TYPE_DIRECTX, d3d_device),
      d3d_device(d3d_device) {
  }

  nvenc_d3d11::~nvenc_d3d11() {
    if (encoder) destroy_encoder();

    if (dll) {
      FreeLibrary(dll);
      dll = NULL;
    }
  }

  ID3D11Texture2D *
  nvenc_d3d11::get_input_texture() {
    return d3d_input_texture.GetInterfacePtr();
  }

  bool
  nvenc_d3d11::init_library() {
    if (dll) return true;

  #ifdef _WIN64
    auto dll_name = "nvEncodeAPI64.dll";
  #else
    auto dll_name = "nvEncodeAPI.dll";
  #endif

    if ((dll = LoadLibraryEx(dll_name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32))) {
      if (auto create_instance = (decltype(NvEncodeAPICreateInstance) *) GetProcAddress(dll, "NvEncodeAPICreateInstance")) {
        auto new_nvenc = std::make_unique<NV_ENCODE_API_FUNCTION_LIST>();
        new_nvenc->version = min_struct_version(NV_ENCODE_API_FUNCTION_LIST_VER);
        if (nvenc_failed(create_instance(new_nvenc.get()))) {
          BOOST_LOG(error) << "NvEncodeAPICreateInstance failed: " << last_error_string;
        }
        else {
          nvenc = std::move(new_nvenc);
          return true;
        }
      }
      else {
        BOOST_LOG(error) << "No NvEncodeAPICreateInstance in " << dll_name;
      }
    }
    else {
      BOOST_LOG(debug) << "Couldn't load NvEnc library " << dll_name;
    }

    if (dll) {
      FreeLibrary(dll);
      dll = NULL;
    }

    return false;
  }

  bool
  nvenc_d3d11::create_and_register_input_buffer() {
    if (!d3d_input_texture) {
      D3D11_TEXTURE2D_DESC desc = {};
      desc.Width = encoder_params.width;
      desc.Height = encoder_params.height;
      desc.MipLevels = 1;
      desc.ArraySize = 1;
      desc.Format = dxgi_format_from_nvenc_format(encoder_params.buffer_format);
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_RENDER_TARGET;
      if (d3d_device->CreateTexture2D(&desc, nullptr, &d3d_input_texture) != S_OK) {
        BOOST_LOG(error) << "NvEnc: couldn't create input texture";
        return false;
      }
    }

    if (!registered_input_buffer) {
      NV_ENC_REGISTER_RESOURCE register_resource = { min_struct_version(NV_ENC_REGISTER_RESOURCE_VER, 3, 4) };
      register_resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
      register_resource.width = encoder_params.width;
      register_resource.height = encoder_params.height;
      register_resource.resourceToRegister = d3d_input_texture.GetInterfacePtr();
      register_resource.bufferFormat = encoder_params.buffer_format;
      register_resource.bufferUsage = NV_ENC_INPUT_IMAGE;

      if (nvenc_failed(nvenc->nvEncRegisterResource(encoder, &register_resource))) {
        BOOST_LOG(error) << "NvEncRegisterResource failed: " << last_error_string;
        return false;
      }

      registered_input_buffer = register_resource.registeredResource;
    }

    return true;
  }

}  // namespace nvenc
#endif
