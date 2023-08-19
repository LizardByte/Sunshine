#pragma once
#ifdef _WIN32

  #include <comdef.h>
  #include <d3d11.h>

  #include "nvenc_base.h"

namespace nvenc {

  _COM_SMARTPTR_TYPEDEF(ID3D11Device, IID_ID3D11Device);
  _COM_SMARTPTR_TYPEDEF(ID3D11Texture2D, IID_ID3D11Texture2D);
  _COM_SMARTPTR_TYPEDEF(IDXGIDevice, IID_IDXGIDevice);
  _COM_SMARTPTR_TYPEDEF(IDXGIAdapter, IID_IDXGIAdapter);

  class nvenc_d3d11: public nvenc_base {
  public:
    nvenc_d3d11(NV_ENC_DEVICE_TYPE device_type):
        nvenc_base(device_type) {}

    virtual ~nvenc_d3d11();

    virtual ID3D11Texture2D *
    get_input_texture() = 0;

  protected:
    bool
    init_library() override;

    HMODULE dll = NULL;
  };

}  // namespace nvenc
#endif
