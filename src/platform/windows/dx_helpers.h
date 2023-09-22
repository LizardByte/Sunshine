#pragma once

#include "display.h"

namespace platf::dxgi {

  DXGI_FORMAT
  unorm_from_typeless_texture_format(DXGI_FORMAT typeless_format);
  DXGI_FORMAT
  srgb_from_typeless_texture_format(DXGI_FORMAT typeless_format);
  DXGI_FORMAT
  typeless_from_unorm_texture_format(DXGI_FORMAT unorm_format);

  bool
  update_vertex_shader_on_blob_mismatch(ID3D11Device *device, ID3D11VertexShaderPtr &target, ID3DBlobPtr &target_hlsl, ID3DBlob *new_hlsl);
  bool
  update_pixel_shader_on_blob_mismatch(ID3D11Device *device, ID3D11PixelShaderPtr &target, ID3DBlobPtr &target_hlsl, ID3DBlob *new_hlsl);

}  // namespace platf::dxgi
