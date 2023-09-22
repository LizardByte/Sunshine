#include "dx_helpers.h"

namespace {

  using namespace platf::dxgi;

  template <typename target_type, typename create_function_type>
  bool
  update_shader_on_blob_mismatch(create_function_type create_function, ID3D11Device *device, target_type &target, ID3DBlobPtr &target_hlsl, ID3DBlob *new_hlsl) {
    if (!device) {
      BOOST_LOG(error) << "update_shader_on_blob_mismatch() called with null device";
      return false;
    }
    if (target_hlsl != new_hlsl) {
      target = nullptr;
      if (new_hlsl) {
        auto status = (device->*create_function)(new_hlsl->GetBufferPointer(), new_hlsl->GetBufferSize(), nullptr, &target);
        if (FAILED(status)) {
          BOOST_LOG(error) << "Failed to create shader from hlsl blob [0x" << util::hex(status).to_string_view() << ']';
          return false;
        }
      }
      target_hlsl = new_hlsl;
    }
    return true;
  }

}  // namespace

namespace platf::dxgi {

  DXGI_FORMAT
  unorm_from_typeless_texture_format(DXGI_FORMAT typeless_format) {
    switch (typeless_format) {
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM;

      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return DXGI_FORMAT_B8G8R8X8_UNORM;

      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;

      default:
        return DXGI_FORMAT_UNKNOWN;
    }
  }

  DXGI_FORMAT
  srgb_from_typeless_texture_format(DXGI_FORMAT typeless_format) {
    switch (typeless_format) {
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;

      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

      default:
        return DXGI_FORMAT_UNKNOWN;
    }
  }

  DXGI_FORMAT
  typeless_from_unorm_texture_format(DXGI_FORMAT unorm_format) {
    switch (unorm_format) {
      case DXGI_FORMAT_B8G8R8A8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;

      case DXGI_FORMAT_B8G8R8X8_UNORM:
        return DXGI_FORMAT_B8G8R8X8_TYPELESS;

      case DXGI_FORMAT_R8G8B8A8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;

      default:
        return DXGI_FORMAT_UNKNOWN;
    }
  }

  bool
  update_vertex_shader_on_blob_mismatch(ID3D11Device *device, ID3D11VertexShaderPtr &target, ID3DBlobPtr &target_hlsl, ID3DBlob *new_hlsl) {
    return update_shader_on_blob_mismatch(&ID3D11Device::CreateVertexShader, device, target, target_hlsl, new_hlsl);
  }

  bool
  update_pixel_shader_on_blob_mismatch(ID3D11Device *device, ID3D11PixelShaderPtr &target, ID3DBlobPtr &target_hlsl, ID3DBlob *new_hlsl) {
    return update_shader_on_blob_mismatch(&ID3D11Device::CreatePixelShader, device, target, target_hlsl, new_hlsl);
  }

}  // namespace platf::dxgi
