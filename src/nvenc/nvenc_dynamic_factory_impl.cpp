/**
 * @file src/nvenc/nvenc_dynamic_factory_impl.cpp
 * @brief SDK-specific constructors used by the runtime NVENC factory.
 */
#ifdef _WIN32

  #ifndef NVENC_NAMESPACE
    #error NVENC_NAMESPACE must identify the version-specific implementation namespace
  #endif

  #ifndef NVENC_FACTORY_SUFFIX
    #error NVENC_FACTORY_SUFFIX must identify the version-specific constructor suffix
  #endif

  // standard includes
  #include <utility>

  // local includes
  #include "nvenc_d3d11_native.h"
  #include "nvenc_d3d11_on_cuda.h"
  #include "nvenc_dynamic_factory_versions.h"

  #ifndef DOXYGEN
    #define NVENC_CONCAT_IMPL(left, right) left##right
    #define NVENC_CONCAT(left, right) NVENC_CONCAT_IMPL(left, right)

namespace nvenc::detail {

  std::unique_ptr<nvenc_d3d11_interface> NVENC_CONCAT(create_nvenc_d3d11_native_, NVENC_FACTORY_SUFFIX)(
    ID3D11Device *device,
    shared_dll dll
  ) {
    return std::make_unique<NVENC_NAMESPACE::nvenc_d3d11_native>(device, std::move(dll));
  }

  std::unique_ptr<nvenc_d3d11_interface> NVENC_CONCAT(create_nvenc_d3d11_on_cuda_, NVENC_FACTORY_SUFFIX)(
    ID3D11Device *device,
    shared_dll dll
  ) {
    return std::make_unique<NVENC_NAMESPACE::nvenc_d3d11_on_cuda>(device, std::move(dll));
  }

}  // namespace nvenc::detail

    #undef NVENC_CONCAT
    #undef NVENC_CONCAT_IMPL
  #endif
#endif
