/**
 * @file tests/unit/test_nvapi_opensource_wrapper.cpp
 * @brief Tests for immutable NVAPI tables and loaded-driver lifetime.
 */
#ifdef _WIN32

  // standard includes
  #include <string>

  // lib includes
  #include <gtest/gtest.h>

  // local includes
  #include "src/platform/windows/nvprefs/nvapi_opensource_wrapper.h"

namespace {
  /**
   * @brief SDK module-handle object whose derived marker verifies injected resolver arguments.
   */
  struct fake_library_t: std::remove_pointer_t<HMODULE> {
    NvU32 marker;  ///< Driver identity observed by the test resolver.
  };

  /**
   * @brief Own a fake driver allocation behind a simulated native module handle.
   *
   * @param marker Driver identity used to verify resolver arguments.
   * @param unload Observer copied into ownership and notified when the last library reference is released.
   * @return Shared module handle retaining the typed fake allocation.
   */
  nvprefs::api_t::library_t fake_library(NvU32 marker, const std::function<void()> &unload) {
    auto allocation = std::shared_ptr<fake_library_t>(new fake_library_t {{}, marker}, [unload](fake_library_t *library) {
      delete library;
      unload();
    });
    return {allocation, allocation.get()};
  }

  /**
   * @brief Return a status and output value through an injected driver interface.
   *
   * @param value Value forwarded by the API wrapper.
   * @param output Receives the incremented value.
   * @return A deterministic driver status.
   */
  NvAPI_Status __cdecl fake_interface(NvU32 value, NvU32 *output) {
    *output = value + 1;
    return NVAPI_INVALID_ARGUMENT;
  }

  /**
   * @brief Simulate a driver that exposes its resolver but supports none of the interfaces.
   *
   * @param id Requested interface identifier.
   * @return nullptr for each unsupported driver interface.
   */
  FARPROC unsupported_query_interface([[maybe_unused]] NvU32 id) {
    return nullptr;
  }
}  // namespace

/**
 * @brief Exercise the global C-linked native resolver ABI before converting its function address.
 *
 * @param id Requested interface identifier.
 * @return Driver interface address for ID 17, or nullptr for an unsupported interface.
 */
extern "C" void *__cdecl native_query_interface(NvU32 id) {
  return id == 17u ? std::bit_cast<void *>(&fake_interface) : nullptr;
}

/**
 * @brief A missing native resolver produces an empty typed query.
 */
TEST(NvapiApiTest, NativeQueryAdapterRejectsMissingExport) {
  EXPECT_FALSE(nvprefs::api_t::adapt_query(nullptr));
}

/**
 * @brief The native resolver adapter preserves unsupported results and callable interface addresses.
 */
TEST(NvapiApiTest, NativeQueryAdapterPreservesResolverAbiAndFunctionAddresses) {
  const auto query = nvprefs::api_t::adapt_query(std::bit_cast<FARPROC>(&native_query_interface));
  ASSERT_TRUE(query);
  EXPECT_EQ(query(18u), nullptr);
  const auto address = query(17u);
  ASSERT_NE(address, nullptr);
  EXPECT_EQ(address, std::bit_cast<FARPROC>(&fake_interface));
  NvU32 output = 0;
  const auto resolved_interface = std::bit_cast<decltype(fake_interface) *>(address);
  EXPECT_EQ(resolved_interface, &fake_interface);
  EXPECT_EQ(resolved_interface(41u, &output), NVAPI_INVALID_ARGUMENT);
  EXPECT_EQ(output, 42u);
}

TEST(NvapiApiTest, EmptyTableReportsNotInitialized) {
  const nvprefs::api_t api {{}, {}};
  NvU32 output = 0;
  EXPECT_EQ(api.call_interface<decltype(fake_interface)>("missing", 4u, &output), NVAPI_API_NOT_INITIALIZED);
  EXPECT_EQ(output, 0u);
}

TEST(NvapiApiTest, MissingAndNullInterfacesReportNotSupported) {
  const nvprefs::api_t api {{}, {{"null", nullptr}}};
  NvU32 output = 0;
  EXPECT_EQ(api.call_interface<decltype(fake_interface)>("missing", 4u, &output), NVAPI_NOT_SUPPORTED);
  EXPECT_EQ(api.call_interface<decltype(fake_interface)>("null", 4u, &output), NVAPI_NOT_SUPPORTED);
  EXPECT_EQ(output, 0u);
}

TEST(NvapiApiTest, LooksUpNamesByValueAndForwardsArguments) {
  const nvprefs::api_t api {{}, {{"fake_interface", std::bit_cast<FARPROC>(&fake_interface)}}};
  const std::string separately_allocated_name {"fake_interface"};
  NvU32 output = 0;
  EXPECT_EQ(api.call_interface<decltype(fake_interface)>(separately_allocated_name, 41u, &output), NVAPI_INVALID_ARGUMENT);
  EXPECT_EQ(output, 42u);
}

TEST(NvapiApiTest, RetainsTheLibraryUntilItsLastApiOwnerIsReleased) {
  bool unloaded = false;
  auto library = fake_library(1, [&unloaded]() {
    unloaded = true;
  });
  auto api = std::make_shared<const nvprefs::api_t>(library, nvprefs::api_t::interfaces_t {{"fake", std::bit_cast<FARPROC>(&fake_interface)}});
  library.reset();
  EXPECT_FALSE(unloaded);
  NvU32 output = 0;
  EXPECT_EQ(api->call_interface<decltype(fake_interface)>("fake", 8u, &output), NVAPI_INVALID_ARGUMENT);
  EXPECT_EQ(output, 9u);
  api.reset();
  EXPECT_TRUE(unloaded);
}

TEST(NvapiApiTest, MissingLibrarySkipsSymbolLookup) {
  bool resolved = false;
  const nvprefs::api_t::loader_t loader {
    [](const char *) -> nvprefs::api_t::library_t {
      return {};
    },
    [&resolved](HMODULE, const char *) -> nvprefs::api_t::query_t {
      resolved = true;
      return {};
    },
  };
  EXPECT_FALSE(nvprefs::api_t::load(loader));
  EXPECT_FALSE(resolved);
}

TEST(NvapiApiTest, MissingResolverReleasesTheLibrary) {
  bool unloaded = false;
  const nvprefs::api_t::loader_t loader {
    [&unloaded](const char *) {
      return fake_library(7, [&unloaded]() {
        unloaded = true;
      });
    },
    [](HMODULE module, const char *name) -> nvprefs::api_t::query_t {
      EXPECT_EQ(static_cast<fake_library_t *>(module)->marker, 7u);
      EXPECT_STREQ(name, "nvapi_QueryInterface");
      return {};
    },
  };
  EXPECT_FALSE(nvprefs::api_t::load(loader));
  EXPECT_TRUE(unloaded);
}

TEST(NvapiApiTest, FailedLoadingCanBeRetriedWithoutPublishingPartialState) {
  int attempts = 0;
  int unloads = 0;
  const nvprefs::api_t::loader_t loader {
    [&attempts, &unloads](const char *name) -> nvprefs::api_t::library_t {
  #if defined(_M_ARM64) || defined(__aarch64__)
      EXPECT_STREQ(name, "nvapia64.dll");
  #elif defined(_WIN64)
      EXPECT_STREQ(name, "nvapi64.dll");
  #else
      EXPECT_STREQ(name, "nvapi.dll");
  #endif
      if (++attempts == 1) {
        return {};
      }
      return fake_library(11, [&unloads]() {
        ++unloads;
      });
    },
    [](HMODULE module, const char *name) {
      EXPECT_EQ(static_cast<fake_library_t *>(module)->marker, 11u);
      EXPECT_STREQ(name, "nvapi_QueryInterface");
      return nvprefs::api_t::query_t {unsupported_query_interface};
    },
  };
  EXPECT_FALSE(nvprefs::api_t::load(loader));
  auto api = nvprefs::api_t::load(loader);
  ASSERT_TRUE(api);
  NvU32 output = 0;
  EXPECT_EQ(api->call_interface<decltype(fake_interface)>("missing", 4u, &output), NVAPI_NOT_SUPPORTED);
  EXPECT_EQ(attempts, 2);
  EXPECT_EQ(unloads, 0);
  api.reset();
  EXPECT_EQ(unloads, 1);
}

#endif
