/**
 * @file tests/unit/test_nvenc_dynamic_factory.cpp
 * @brief Tests for the Windows runtime NVENC SDK factory.
 */
#ifdef _WIN32

  // standard includes
  #include <array>
  #include <bit>
  #include <cstdint>
  #include <memory>
  #include <string_view>
  #include <utility>

  // lib includes
  #include <gtest/gtest.h>

  // local includes
  #include "src/nvenc/nvenc_dynamic_factory.h"
  #include "src/video.h"

namespace {

  std::uint32_t reported_version;  ///< Version returned by the fake NVENC driver.
  std::uint32_t reported_status;  ///< Status returned by the fake NVENC driver.

  /**
   * @brief Fake implementation of `NvEncodeAPIGetMaxSupportedVersion()`.
   *
   * @param version Receives the configured packed API version.
   * @return Configured NVENC status code.
   */
  std::uint32_t WINAPI get_fake_max_supported_version(std::uint32_t *version) {
    *version = reported_version;
    return reported_status;
  }

  /**
   * @brief Minimal SDK-neutral encoder used to verify factory callbacks.
   */
  class fake_nvenc_encoder final: public nvenc::nvenc_d3d11_interface {
  public:
    bool create_encoder(
      const nvenc::nvenc_config &,
      const video::config_t &,
      const video::sunshine_colorspace_t &,
      platf::pix_fmt_e
    ) override {
      return true;
    }

    void destroy_encoder() override {
    }

    nvenc::nvenc_encoded_frame encode_frame(std::uint64_t frame_index, bool force_idr) override {
      return {{}, frame_index, force_idr, false};
    }

    bool invalidate_ref_frames(std::uint64_t, std::uint64_t) override {
      return true;
    }

    video::bitrate_reconfigure_result_t reconfigure_bitrate(std::uint32_t target_kbps) override {
      return {
        video::bitrate_reconfigure_status_e::unsupported,
        0,
        target_kbps,
        0,
      };
    }

    ID3D11Texture2D *get_input_texture() override {
      return nullptr;
    }
  };

  /**
   * @brief Make a non-owning fake Windows module handle for factory discovery tests.
   *
   * @return Shared fake module handle.
   */
  nvenc::shared_dll make_fake_dll() {
    constexpr std::uintptr_t fake_handle_value = 1U;
    return {
      reinterpret_cast<HMODULE>(fake_handle_value),
      [](HMODULE) {
      },
    };
  }

  /**
   * @brief Make runtime operations that expose the fake version query function.
   *
   * @return Runtime operations for a successfully loaded fake NVENC driver.
   */
  nvenc::nvenc_runtime_api make_fake_runtime_api() {
    return {
      []() {
        return make_fake_dll();
      },
      [](HMODULE dll, const char *symbol) {
        EXPECT_EQ(dll, make_fake_dll().get());
        EXPECT_EQ(std::string_view {symbol}, "NvEncodeAPIGetMaxSupportedVersion");
        return std::bit_cast<FARPROC>(&get_fake_max_supported_version);
      },
    };
  }

  TEST(NvencDynamicFactoryTest, HandlesUnavailableDriver) {
    bool resolved_symbol = false;
    const nvenc::nvenc_runtime_api runtime_api {
      []() {
        return nvenc::shared_dll {};
      },
      [&resolved_symbol](HMODULE, const char *) {
        resolved_symbol = true;
        return FARPROC {};
      },
    };

    EXPECT_FALSE(nvenc::nvenc_dynamic_factory::get(runtime_api));
    EXPECT_FALSE(resolved_symbol);
  }

  TEST(NvencDynamicFactoryTest, HandlesMissingVersionQuery) {
    const nvenc::nvenc_runtime_api runtime_api {
      []() {
        return make_fake_dll();
      },
      [](HMODULE, const char *) {
        return FARPROC {};
      },
    };

    EXPECT_FALSE(nvenc::nvenc_dynamic_factory::get(runtime_api));
  }

  TEST(NvencDynamicFactoryTest, HandlesFailedVersionQuery) {
    reported_version = 1300U;
    reported_status = 1U;

    EXPECT_FALSE(nvenc::nvenc_dynamic_factory::get(make_fake_runtime_api()));
  }

  TEST(NvencDynamicFactoryTest, RejectsUnsupportedDriver) {
    reported_version = 10U << 4U;
    reported_status = 0U;

    EXPECT_FALSE(nvenc::nvenc_dynamic_factory::get(make_fake_runtime_api()));
  }

  TEST(NvencDynamicFactoryTest, SelectsSupportedSdkImplementations) {
    using enum nvenc::nvenc_sdk_version;
    constexpr std::array test_cases {
      std::pair {11U << 4U, sdk_11_0},
      std::pair {12U << 4U, sdk_12_0},
      std::pair {13U << 4U, sdk_13_0},
      std::pair {14U << 4U, sdk_13_0},
    };
    reported_status = 0U;

    for (const auto &[version, expected] : test_cases) {
      reported_version = version;
      const auto factory = nvenc::nvenc_dynamic_factory::get(make_fake_runtime_api());
      ASSERT_TRUE(factory);
      EXPECT_EQ(factory->sdk_version(), expected);
    }
  }

  TEST(NvencDynamicFactoryTest, UsesConfiguredEncoderConstructors) {
    bool created_native = false;
    bool created_on_cuda = false;
    const auto dll = make_fake_dll();
    nvenc::nvenc_dynamic_factory factory {
      dll,
      nvenc::nvenc_sdk_version::sdk_13_0,
      [&created_native, &dll](ID3D11Device *, nvenc::shared_dll callback_dll) {
        created_native = true;
        EXPECT_EQ(callback_dll, dll);
        return std::make_unique<fake_nvenc_encoder>();
      },
      [&created_on_cuda, &dll](ID3D11Device *, nvenc::shared_dll callback_dll) {
        created_on_cuda = true;
        EXPECT_EQ(callback_dll, dll);
        return std::make_unique<fake_nvenc_encoder>();
      },
    };

    EXPECT_EQ(factory.sdk_version(), nvenc::nvenc_sdk_version::sdk_13_0);
    EXPECT_TRUE(factory.create_nvenc_d3d11_native(nullptr));
    EXPECT_TRUE(factory.create_nvenc_d3d11_on_cuda(nullptr));
    EXPECT_TRUE(created_native);
    EXPECT_TRUE(created_on_cuda);
  }

  TEST(NvencDynamicFactoryTest, UsesDefaultWindowsRuntime) {
    const auto factory = nvenc::nvenc_dynamic_factory::get();
    EXPECT_TRUE(!factory || factory->sdk_version() != nvenc::nvenc_sdk_version::unsupported);
  }

  TEST(NvencSharedDllTest, OwnsLoadedModuleUntilLastReference) {
    const auto handle = LoadLibraryEx("version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    ASSERT_NE(handle, nullptr);

    auto dll = nvenc::make_shared_dll(handle);
    EXPECT_EQ(dll.get(), handle);
    auto copy = dll;
    EXPECT_EQ(copy.use_count(), 2);

    dll.reset();
    EXPECT_EQ(copy.use_count(), 1);
    copy.reset();
  }

  TEST(NvencSharedDllTest, AcceptsNullModule) {
    auto dll = nvenc::make_shared_dll(nullptr);
    EXPECT_FALSE(dll);
    dll.reset();
  }

}  // namespace
#endif
