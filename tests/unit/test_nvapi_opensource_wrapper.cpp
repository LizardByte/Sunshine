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
  void *__cdecl unsupported_query_interface([[maybe_unused]] NvU32 id) {
    return nullptr;
  }
}  // namespace

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
  const nvprefs::api_t api {{}, {{"fake_interface", reinterpret_cast<void *>(&fake_interface)}}};
  const std::string separately_allocated_name {"fake_interface"};
  NvU32 output = 0;
  EXPECT_EQ(api.call_interface<decltype(fake_interface)>(separately_allocated_name, 41u, &output), NVAPI_INVALID_ARGUMENT);
  EXPECT_EQ(output, 42u);
}

TEST(NvapiApiTest, RetainsTheLibraryUntilItsLastApiOwnerIsReleased) {
  bool unloaded = false;
  auto library = std::shared_ptr<void>(new int(1), [&unloaded](void *value) {
    unloaded = true;
    delete static_cast<int *>(value);
  });
  auto api = std::make_shared<const nvprefs::api_t>(library, nvprefs::api_t::interfaces_t {{"fake", reinterpret_cast<void *>(&fake_interface)}});
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
    [](const char *) -> std::shared_ptr<void> {
      return {};
    },
    [&resolved](void *, const char *) -> void * {
      resolved = true;
      return nullptr;
    },
  };
  EXPECT_FALSE(nvprefs::api_t::load(loader));
  EXPECT_FALSE(resolved);
}

TEST(NvapiApiTest, MissingResolverReleasesTheLibrary) {
  bool unloaded = false;
  const nvprefs::api_t::loader_t loader {
    [&unloaded](const char *) {
      return std::shared_ptr<void>(new int(7), [&unloaded](void *value) {
        unloaded = true;
        delete static_cast<int *>(value);
      });
    },
    [](void *module, const char *name) -> void * {
      EXPECT_EQ(*static_cast<int *>(module), 7);
      EXPECT_STREQ(name, "nvapi_QueryInterface");
      return nullptr;
    },
  };
  EXPECT_FALSE(nvprefs::api_t::load(loader));
  EXPECT_TRUE(unloaded);
}

TEST(NvapiApiTest, FailedLoadingCanBeRetriedWithoutPublishingPartialState) {
  int attempts = 0;
  int unloads = 0;
  const nvprefs::api_t::loader_t loader {
    [&attempts, &unloads](const char *name) -> std::shared_ptr<void> {
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
      return {new int(11), [&unloads](void *value) {
                ++unloads;
                delete static_cast<int *>(value);
              }};
    },
    [](void *module, const char *name) {
      EXPECT_EQ(*static_cast<int *>(module), 11);
      EXPECT_STREQ(name, "nvapi_QueryInterface");
      return reinterpret_cast<void *>(&unsupported_query_interface);
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
