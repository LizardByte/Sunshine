/**
 * @file src/nvenc/nvenc_d3d11.h
 * @brief Declarations for abstract Direct3D11 NVENC encoder.
 */
#pragma once
#ifdef _WIN32

  // standard includes
  #include <comdef.h>
  #include <d3d11.h>

  // local includes
  #include "nvenc_base.h"
  #include "nvenc_d3d11_interface.h"
  #include "nvenc_shared_dll.h"

namespace NVENC_NAMESPACE {

  #ifdef DOXYGEN
  /**
   * @brief COM smart pointer for ID3D11Device.
   */
  using ID3D11DevicePtr = ID3D11Device *;
  /**
   * @brief COM smart pointer for ID3D11Texture2D.
   */
  using ID3D11Texture2DPtr = ID3D11Texture2D *;
  /**
   * @brief COM smart pointer for IDXGIDevice.
   */
  using IDXGIDevicePtr = IDXGIDevice *;
  /**
   * @brief COM smart pointer for IDXGIAdapter.
   */
  using IDXGIAdapterPtr = IDXGIAdapter *;
  #else
  _COM_SMARTPTR_TYPEDEF(ID3D11Device, IID_ID3D11Device);
  _COM_SMARTPTR_TYPEDEF(ID3D11Texture2D, IID_ID3D11Texture2D);
  _COM_SMARTPTR_TYPEDEF(IDXGIDevice, IID_IDXGIDevice);
  _COM_SMARTPTR_TYPEDEF(IDXGIAdapter, IID_IDXGIAdapter);
  #endif

  /**
   * @brief Abstract Direct3D11 NVENC encoder.
   *        Encapsulates common code used by native and interop implementations.
   */
  class nvenc_d3d11: public nvenc_base, public ::nvenc::nvenc_d3d11_interface {
  public:
    /**
     * @brief Initialize an NVENC session wrapper for D3D11 input textures.
     *
     * @param device_type NVENC device type used by the encoder session.
     * @param dll Shared NVENC driver module.
     */
    explicit nvenc_d3d11(NV_ENC_DEVICE_TYPE device_type, ::nvenc::shared_dll dll);
    ~nvenc_d3d11() override;

    /**
     * @brief Get input surface texture.
     * @return Input surface texture.
     */
    ID3D11Texture2D *get_input_texture() override = 0;

  protected:
    bool init_library() override;
    bool wait_for_async_event(uint32_t timeout_ms) override;

  private:
    ::nvenc::shared_dll dll;
  };

}  // namespace NVENC_NAMESPACE
#endif
