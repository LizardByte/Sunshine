/**
 * @file src/nvenc/nvenc_dynamic_factory.h
 * @brief Declarations for runtime NVENC SDK selection on Windows.
 */
#pragma once
#ifdef _WIN32

// standard includes
  #include <functional>
  #include <memory>

  // local includes
  #include "nvenc_d3d11_interface.h"
  #include "nvenc_shared_dll.h"
  #include "nvenc_version.h"

namespace nvenc {

  /**
   * @brief Factory bound to the newest NVENC SDK supported by the installed driver.
   */
  class nvenc_dynamic_factory {
  public:
    using create_encoder_fn = std::function<std::unique_ptr<nvenc_d3d11_interface>(ID3D11Device *, shared_dll)>;  ///< SDK-specific encoder constructor.

    /**
     * @brief Construct a factory bound to one NVENC SDK implementation.
     *
     * @param dll Shared NVENC driver module.
     * @param sdk_version Selected SDK version.
     * @param create_native Native Direct3D11 encoder constructor.
     * @param create_on_cuda CUDA-interoperability encoder constructor.
     */
    nvenc_dynamic_factory(
      shared_dll dll,
      nvenc_sdk_version sdk_version,
      create_encoder_fn create_native,
      create_encoder_fn create_on_cuda
    );

    /**
     * @brief Load the NVENC driver and select a compatible SDK implementation.
     *
     * @return Initialized factory, or an empty pointer when NVENC is unavailable.
     */
    static std::shared_ptr<nvenc_dynamic_factory> get();

    /**
     * @brief Create a native Direct3D11 NVENC encoder.
     *
     * @param d3d_device Direct3D11 device used for encoding.
     * @return SDK-neutral encoder instance.
     */
    std::unique_ptr<nvenc_d3d11_interface> create_nvenc_d3d11_native(ID3D11Device *d3d_device) const;

    /**
     * @brief Create a CUDA NVENC encoder with Direct3D11 input surfaces.
     *
     * @param d3d_device Direct3D11 device used to create input textures.
     * @return SDK-neutral encoder instance.
     */
    std::unique_ptr<nvenc_d3d11_interface> create_nvenc_d3d11_on_cuda(ID3D11Device *d3d_device) const;

    /**
     * @brief Get the SDK implementation selected for this factory.
     *
     * @return Selected NVENC SDK version.
     */
    nvenc_sdk_version sdk_version() const;

  private:
    shared_dll dll;
    nvenc_sdk_version selected_sdk_version;
    create_encoder_fn create_native;
    create_encoder_fn create_on_cuda;
  };

}  // namespace nvenc
#endif
