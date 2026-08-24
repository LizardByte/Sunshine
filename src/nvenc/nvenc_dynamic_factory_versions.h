/**
 * @file src/nvenc/nvenc_dynamic_factory_versions.h
 * @brief Declarations for SDK-specific NVENC encoder constructors.
 */
#pragma once
#ifdef _WIN32

  // standard includes
  #include <memory>

  // local includes
  #include "nvenc_d3d11_interface.h"
  #include "nvenc_shared_dll.h"

namespace nvenc::detail {

  /**
   * @brief Create an SDK 11.0 native Direct3D11 encoder.
   *
   * @param device Direct3D11 device used for encoding.
   * @param dll Shared NVENC driver module.
   * @return SDK-neutral encoder instance.
   */
  std::unique_ptr<nvenc_d3d11_interface> create_nvenc_d3d11_native_1100(ID3D11Device *device, shared_dll dll);
  /**
   * @brief Create an SDK 11.0 CUDA-interoperability encoder.
   *
   * @param device Direct3D11 device used for input surfaces.
   * @param dll Shared NVENC driver module.
   * @return SDK-neutral encoder instance.
   */
  std::unique_ptr<nvenc_d3d11_interface> create_nvenc_d3d11_on_cuda_1100(ID3D11Device *device, shared_dll dll);
  /**
   * @brief Create an SDK 12.0 native Direct3D11 encoder.
   *
   * @param device Direct3D11 device used for encoding.
   * @param dll Shared NVENC driver module.
   * @return SDK-neutral encoder instance.
   */
  std::unique_ptr<nvenc_d3d11_interface> create_nvenc_d3d11_native_1200(ID3D11Device *device, shared_dll dll);
  /**
   * @brief Create an SDK 12.0 CUDA-interoperability encoder.
   *
   * @param device Direct3D11 device used for input surfaces.
   * @param dll Shared NVENC driver module.
   * @return SDK-neutral encoder instance.
   */
  std::unique_ptr<nvenc_d3d11_interface> create_nvenc_d3d11_on_cuda_1200(ID3D11Device *device, shared_dll dll);
  /**
   * @brief Create an SDK 13.0 native Direct3D11 encoder.
   *
   * @param device Direct3D11 device used for encoding.
   * @param dll Shared NVENC driver module.
   * @return SDK-neutral encoder instance.
   */
  std::unique_ptr<nvenc_d3d11_interface> create_nvenc_d3d11_native_1300(ID3D11Device *device, shared_dll dll);
  /**
   * @brief Create an SDK 13.0 CUDA-interoperability encoder.
   *
   * @param device Direct3D11 device used for input surfaces.
   * @param dll Shared NVENC driver module.
   * @return SDK-neutral encoder instance.
   */
  std::unique_ptr<nvenc_d3d11_interface> create_nvenc_d3d11_on_cuda_1300(ID3D11Device *device, shared_dll dll);

}  // namespace nvenc::detail
#endif
