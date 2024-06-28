/**
 * @file src/nvenc/nvenc_d3d11.h
 * @brief Declarations for base NVENC d3d11.
 */
#pragma once
#ifdef _WIN32

  #include <comdef.h>
  #include <d3d11.h>

  #include "nvenc_base.h"

namespace nvenc {

  _COM_SMARTPTR_TYPEDEF(ID3D11Device, IID_ID3D11Device);
  _COM_SMARTPTR_TYPEDEF(ID3D11Texture2D, IID_ID3D11Texture2D);

  class nvenc_d3d11 final: public nvenc_base {
  public:
    nvenc_d3d11(ID3D11Device *d3d_device);
    ~nvenc_d3d11();

    ID3D11Texture2D *
    get_input_texture();

  private:
    bool
    init_library() override;

    bool
    create_and_register_input_buffer() override;

    HMODULE dll = NULL;
    const ID3D11DevicePtr d3d_device;
    ID3D11Texture2DPtr d3d_input_texture;
  };

}  // namespace nvenc
#endif
