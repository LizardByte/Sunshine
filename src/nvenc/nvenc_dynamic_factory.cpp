/**
 * @file src/nvenc/nvenc_dynamic_factory.cpp
 * @brief Definitions for runtime NVENC SDK selection on Windows.
 */
#ifdef _WIN32

  // this include
  #include "nvenc_dynamic_factory.h"

  // standard includes
  #include <utility>

  // local includes
  #include "nvenc_dynamic_factory_versions.h"
  #include "src/logging.h"

namespace {

  #ifdef _WIN64
  constexpr auto nvenc_dll_name = "nvEncodeAPI64.dll";
  #else
  constexpr auto nvenc_dll_name = "nvEncodeAPI.dll";
  #endif

  constexpr auto minimum_driver_version = "456.71";

  using get_max_supported_version_fn = std::uint32_t(WINAPI *)(std::uint32_t *);

}  // namespace

namespace nvenc {

  nvenc_dynamic_factory::nvenc_dynamic_factory(
    shared_dll dll,
    nvenc_sdk_version sdk_version,
    create_encoder_fn create_native,
    create_encoder_fn create_on_cuda
  ):
      dll(std::move(dll)),
      selected_sdk_version(sdk_version),
      create_native(create_native),
      create_on_cuda(create_on_cuda) {
  }

  std::shared_ptr<nvenc_dynamic_factory> nvenc_dynamic_factory::get() {
    auto dll = make_shared_dll(LoadLibraryEx(nvenc_dll_name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
    if (!dll) {
      BOOST_LOG(debug) << "NvEnc: Couldn't load NvEnc library " << nvenc_dll_name;
      return {};
    }

    auto get_max_version = reinterpret_cast<get_max_supported_version_fn>(
      GetProcAddress(dll.get(), "NvEncodeAPIGetMaxSupportedVersion")
    );
    if (!get_max_version) {
      BOOST_LOG(error) << "NvEnc: No NvEncodeAPIGetMaxSupportedVersion() in " << nvenc_dll_name;
      return {};
    }

    std::uint32_t packed_max_version = 0;
    if (get_max_version(&packed_max_version) != 0U) {
      BOOST_LOG(error) << "NvEnc: NvEncodeAPIGetMaxSupportedVersion() failed";
      return {};
    }

    const auto max_version = decode_nvenc_driver_version(packed_max_version);
    const auto sdk_version = select_nvenc_sdk_version(max_version);
    switch (sdk_version) {
      case nvenc_sdk_version::sdk_13_0:
        return std::shared_ptr<nvenc_dynamic_factory>(new nvenc_dynamic_factory(
          std::move(dll),
          sdk_version,
          detail::create_nvenc_d3d11_native_1300,
          detail::create_nvenc_d3d11_on_cuda_1300
        ));

      case nvenc_sdk_version::sdk_12_0:
        return std::shared_ptr<nvenc_dynamic_factory>(new nvenc_dynamic_factory(
          std::move(dll),
          sdk_version,
          detail::create_nvenc_d3d11_native_1200,
          detail::create_nvenc_d3d11_on_cuda_1200
        ));

      case nvenc_sdk_version::sdk_11_0:
        return std::shared_ptr<nvenc_dynamic_factory>(new nvenc_dynamic_factory(
          std::move(dll),
          sdk_version,
          detail::create_nvenc_d3d11_native_1100,
          detail::create_nvenc_d3d11_on_cuda_1100
        ));

      case nvenc_sdk_version::unsupported:
        BOOST_LOG(error) << "NvEnc: minimum required driver version is " << minimum_driver_version;
        return {};
    }

    return {};
  }

  std::unique_ptr<nvenc_d3d11_interface> nvenc_dynamic_factory::create_nvenc_d3d11_native(ID3D11Device *d3d_device) const {
    return create_native(d3d_device, dll);
  }

  std::unique_ptr<nvenc_d3d11_interface> nvenc_dynamic_factory::create_nvenc_d3d11_on_cuda(ID3D11Device *d3d_device) const {
    return create_on_cuda(d3d_device, dll);
  }

  nvenc_sdk_version nvenc_dynamic_factory::sdk_version() const {
    return selected_sdk_version;
  }

}  // namespace nvenc
#endif
