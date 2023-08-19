#pragma once
#ifdef _WIN32

  #include <comdef.h>
  #include <d3d11.h>

  #include "nvenc_d3d11.h"

namespace nvenc {

  class nvenc_d3d11_native final: public nvenc_d3d11 {
  public:
    nvenc_d3d11_native(ID3D11Device *d3d_device);
    ~nvenc_d3d11_native();

    ID3D11Texture2D *
    get_input_texture() override;

  private:
    bool
    create_and_register_input_buffer() override;

    const ID3D11DevicePtr d3d_device;
    ID3D11Texture2DPtr d3d_input_texture;
  };

}  // namespace nvenc
#endif
