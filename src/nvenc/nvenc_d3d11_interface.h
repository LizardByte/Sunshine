/**
 * @file src/nvenc/nvenc_d3d11_interface.h
 * @brief Declarations for the SDK-neutral Direct3D11 NVENC interface.
 */
#pragma once
#ifdef _WIN32

  // lib includes
  #include <d3d11.h>

  // local includes
  #include "nvenc_encoder.h"

namespace nvenc {

  /**
   * @brief SDK-neutral Direct3D11 NVENC encoder interface.
   */
  // Virtual inheritance is required because concrete encoders also inherit their SDK-specific implementation.
  class nvenc_d3d11_interface: public virtual nvenc_encoder {  // NOSONAR(cpp:S1011)
  public:
    /**
     * @brief Destroy the SDK-neutral Direct3D11 encoder interface.
     */
    ~nvenc_d3d11_interface() override = default;

    /**
     * @brief Get the input surface texture.
     *
     * @return Input surface texture.
     */
    virtual ID3D11Texture2D *get_input_texture() = 0;
  };

}  // namespace nvenc
#endif
