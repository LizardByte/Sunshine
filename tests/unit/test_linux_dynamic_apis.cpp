/**
 * @file tests/unit/test_linux_dynamic_apis.cpp
 * @brief Tests for optional Linux API loading and resource ownership.
 */
#ifdef __linux__

  // standard includes
  #include <functional>
  #include <string_view>

  // lib includes
  #include <gtest/gtest.h>

  // local includes
  #include "src/platform/linux/graphics.h"
  #include "src/platform/linux/nvfbc_api.h"

namespace {
  /**
   * @brief Fake library state retained by an opaque loader handle.
   */
  struct fake_library_t {
    int marker;  ///< Value observed by the injected resolver.
    std::function<void()> on_unload;  ///< Observer invoked when the last library owner is released.

    /**
     * @brief Notify the test when the final library handle is released.
     */
    ~fake_library_t() {
      if (on_unload) {
        on_unload();
      }
    }
  };

  /**
   * @brief Create an opaque library handle retaining an injected implementation.
   *
   * @param marker Value exposed to injected resolvers.
   * @param on_unload Observer invoked when the last handle is released.
   * @return Shared opaque handle owning the fake library state.
   */
  std::shared_ptr<dyn::library_handle_t> make_fake_library(int marker, std::function<void()> on_unload = {}) {
    auto owner = std::make_shared<fake_library_t>(marker, std::move(on_unload));
    return {owner, reinterpret_cast<dyn::library_handle_t *>(owner.get())};
  }

  /**
   * @brief Fake device with an instance-specific cleanup observer.
   */
  struct fake_gbm_device_t {
    int fd;  ///< Descriptor supplied to the allocation function.
    std::function<void()> on_destroy;  ///< Observer invoked when the device is released.

    /**
     * @brief Notify the test that the API released its device.
     */
    ~fake_gbm_device_t() {
      if (on_destroy) {
        on_destroy();
      }
    }
  };

  /**
   * @brief Allocate a fake device or simulate an invalid descriptor.
   *
   * @param fd Descriptor supplied by the device owner.
   * @return Opaque fake device, or nullptr for a negative descriptor.
   */
  gbm::device *create_fake_device(int fd) {
    return fd < 0 ? nullptr : reinterpret_cast<gbm::device *>(new fake_gbm_device_t {fd, {}});
  }

  /**
   * @brief Release a fake device through its loaded API.
   *
   * @param value Device created by create_fake_device.
   */
  void destroy_fake_device(gbm::device *value) {
    delete reinterpret_cast<fake_gbm_device_t *>(value);
  }

  /**
   * @brief Resolve fake GBM functions into the unpublished table.
   *
   * @param library Fake loaded-library handle.
   * @param symbols Destinations for the two required functions.
   * @return Success after both destinations have been populated.
   */
  int resolve_fake_gbm([[maybe_unused]] dyn::library_handle_t *library, const dyn::symbols_t &symbols) {
    for (const auto &[destination, name] : symbols) {
      if (std::string_view(name) == "gbm_device_destroy") {
        *destination = reinterpret_cast<dyn::apiproc>(&destroy_fake_device);
      } else if (std::string_view(name) == "gbm_create_device") {
        *destination = reinterpret_cast<dyn::apiproc>(&create_fake_device);
      } else {
        ADD_FAILURE() << "Unexpected GBM symbol: " << name;
        return -1;
      }
    }
    return 0;
  }

  /**
   * @brief Fake successful NvFBC session operation.
   *
   * @tparam Params Parameters for the selected session operation.
   * @param handle Session being operated on.
   * @param params Parameters supplied by the capture owner.
   * @return Success without accessing a GPU.
   */
  template<class Params>
  NVFBCSTATUS NVFBCAPI fake_session_operation([[maybe_unused]] NVFBC_SESSION_HANDLE handle, [[maybe_unused]] Params *params) {
    return NVFBC_SUCCESS;
  }

  /**
   * @brief Fake NvFBC session allocation.
   *
   * @param handle Receives a deterministic session handle.
   * @param params Parameters supplied by the capture owner.
   * @return Success without accessing a GPU.
   */
  NVFBCSTATUS NVFBCAPI fake_create_handle(NVFBC_SESSION_HANDLE *handle, [[maybe_unused]] NVFBC_CREATE_HANDLE_PARAMS *params) {
    *handle = 1;
    return NVFBC_SUCCESS;
  }

  /**
   * @brief Return a deterministic fake-driver error string.
   *
   * @param handle Session being queried.
   * @return Fake error description.
   */
  const char *NVFBCAPI fake_last_error([[maybe_unused]] NVFBC_SESSION_HANDLE handle) {
    return "fake driver";
  }

  /**
   * @brief Populate a complete or deliberately incomplete NvFBC API table.
   *
   * @tparam Missing Index of a required function to omit, or -1 for a complete table.
   * @tparam Status Result returned by API creation.
   * @param functions Function table whose version is set by the API loader.
   * @return Configured creation status.
   */
  template<int Missing = -1, NVFBCSTATUS Status = NVFBC_SUCCESS>
  NVFBCSTATUS NVFBCAPI fake_create_instance(NVFBC_API_FUNCTION_LIST *functions) {
    EXPECT_EQ(functions->dwVersion, NVFBC_VERSION);
    functions->nvFBCCreateHandle = Missing == 0 ? nullptr : &fake_create_handle;
    functions->nvFBCDestroyHandle = Missing == 1 ? nullptr : &fake_session_operation<NVFBC_DESTROY_HANDLE_PARAMS>;
    functions->nvFBCGetStatus = Missing == 2 ? nullptr : &fake_session_operation<NVFBC_GET_STATUS_PARAMS>;
    functions->nvFBCGetLastErrorStr = Missing == 3 ? nullptr : &fake_last_error;
    functions->nvFBCBindContext = Missing == 4 ? nullptr : &fake_session_operation<NVFBC_BIND_CONTEXT_PARAMS>;
    functions->nvFBCReleaseContext = Missing == 5 ? nullptr : &fake_session_operation<NVFBC_RELEASE_CONTEXT_PARAMS>;
    functions->nvFBCCreateCaptureSession = Missing == 6 ? nullptr : &fake_session_operation<NVFBC_CREATE_CAPTURE_SESSION_PARAMS>;
    functions->nvFBCDestroyCaptureSession = Missing == 7 ? nullptr : &fake_session_operation<NVFBC_DESTROY_CAPTURE_SESSION_PARAMS>;
    functions->nvFBCToCudaSetUp = Missing == 8 ? nullptr : &fake_session_operation<NVFBC_TOCUDA_SETUP_PARAMS>;
    functions->nvFBCToCudaGrabFrame = Missing == 9 ? nullptr : &fake_session_operation<NVFBC_TOCUDA_GRAB_FRAME_PARAMS>;
    return Status;
  }

  /**
   * @brief Parameterized failures for each function required by the NvFBC capture owner.
   */
  class NvfbcIncompleteApiTest: public ::testing::TestWithParam<PNVFBCCREATEINSTANCE> {};
}  // namespace

/**
 * @brief A failed library lookup leaves later candidate fallback available.
 */
TEST(LinuxLibraryTest, MissingLibraryCanBeRetriedWithCandidateFallback) {
  const auto loader = dyn::system_loader();
  EXPECT_FALSE(loader.open({"libsunshine-loader-test-does-not-exist.so"}));
  EXPECT_TRUE(loader.open({"libsunshine-loader-test-does-not-exist.so", "libc.so.6", "libc.so"}));
}

/**
 * @brief Resolved symbols stay usable while a shared library owner remains alive.
 */
TEST(LinuxLibraryTest, SystemSymbolsRemainUsableThroughSharedHandleOwnership) {
  const auto loader = dyn::system_loader();
  auto library = loader.open({"libc.so.6", "libc.so"});
  ASSERT_TRUE(library);
  auto retained_library = library;
  const std::weak_ptr<dyn::library_handle_t> weak_library = library;
  library.reset();
  EXPECT_FALSE(weak_library.expired());

  pid_t (*process_id)() {};
  dyn::apiproc missing_symbol {};
  EXPECT_EQ(loader.resolve(retained_library.get(), {{reinterpret_cast<dyn::apiproc *>(&process_id), "getpid"}}), 0);
  ASSERT_NE(process_id, nullptr);
  EXPECT_EQ(process_id(), getpid());
  EXPECT_EQ(loader.resolve(retained_library.get(), {{&missing_symbol, "sunshine_loader_test_nonexistent_symbol"}}), -1);
  EXPECT_EQ(missing_symbol, nullptr);

  retained_library.reset();
  EXPECT_TRUE(weak_library.expired());
}

TEST(GbmApiTest, EmptyApiDoesNotAllocateADevice) {
  EXPECT_FALSE(gbm::make_device({}, 7));
}

TEST(GbmApiTest, DeviceRetainsItsApiAndDestroysBeforeUnloading) {
  bool destroyed = false;
  bool unloaded = false;
  auto library = make_fake_library(1, [&]() {
    EXPECT_TRUE(destroyed);
    unloaded = true;
  });
  auto api = std::make_shared<const gbm::api_t>(library, &destroy_fake_device, &create_fake_device);
  auto device = gbm::make_device(api, 42);
  ASSERT_TRUE(device);
  auto fake = reinterpret_cast<fake_gbm_device_t *>(device.get());
  EXPECT_EQ(fake->fd, 42);
  fake->on_destroy = [&]() {
    EXPECT_FALSE(unloaded);
    destroyed = true;
  };
  library.reset();
  api.reset();
  EXPECT_FALSE(unloaded);
  device = gbm::gbm_t {};
  EXPECT_TRUE(destroyed);
  EXPECT_TRUE(unloaded);
}

TEST(GbmApiTest, DeviceAllocationCanFailWithoutCallingItsDeleter) {
  const auto api = std::make_shared<const gbm::api_t>(nullptr, &destroy_fake_device, &create_fake_device);
  EXPECT_FALSE(gbm::make_device(api, -1));
}

TEST(GbmApiTest, LibraryFailureCanBeRetriedAndPartialSymbolsAreDiscarded) {
  int attempts = 0;
  int unloads = 0;
  int resolutions = 0;
  const dyn::loader_t loader {
    [&](const std::vector<const char *> &names) -> std::shared_ptr<dyn::library_handle_t> {
      EXPECT_STREQ(names[0], "libgbm.so.1");
      EXPECT_STREQ(names[1], "libgbm.so");
      if (++attempts == 1) {
        return {};
      }
      return make_fake_library(7, [&]() {
        ++unloads;
      });
    },
    [&](dyn::library_handle_t *library, const dyn::symbols_t &symbols) {
      EXPECT_EQ(reinterpret_cast<const fake_library_t *>(library)->marker, 7);
      if (++resolutions == 1) {
        *std::get<0>(symbols.front()) = reinterpret_cast<dyn::apiproc>(&destroy_fake_device);
        return -1;
      }
      return resolve_fake_gbm(library, symbols);
    },
  };
  EXPECT_FALSE(gbm::api_t::load(loader));
  EXPECT_EQ(resolutions, 0);
  EXPECT_FALSE(gbm::api_t::load(loader));
  EXPECT_EQ(unloads, 1);
  auto api = gbm::api_t::load(loader);
  ASSERT_TRUE(api);
  EXPECT_EQ(attempts, 3);
  EXPECT_EQ(unloads, 1);
  api.reset();
  EXPECT_EQ(unloads, 2);
}

TEST(GbmApiTest, RejectsIncompleteTablesEvenWhenResolverReportsSuccess) {
  for (const std::string_view missing_name : {"gbm_device_destroy", "gbm_create_device"}) {
    const dyn::loader_t loader {
      [](const std::vector<const char *> &) {
        return make_fake_library(1);
      },
      [&](dyn::library_handle_t *library, const dyn::symbols_t &symbols) {
        resolve_fake_gbm(library, symbols);
        for (const auto &[destination, name] : symbols) {
          if (missing_name == name) {
            *destination = nullptr;
          }
        }
        return 0;
      },
    };
    EXPECT_FALSE(gbm::api_t::load(loader));
  }
}

TEST(NvfbcApiTest, RejectsAnUnresolvedEntryPointEvenWhenResolverReportsSuccess) {
  const dyn::loader_t loader {
    [](const std::vector<const char *> &) {
      return make_fake_library(1);
    },
    [](dyn::library_handle_t *, const dyn::symbols_t &) {
      return 0;
    },
  };
  EXPECT_FALSE(cuda::nvfbc::api_t::load(loader));
}

TEST(NvfbcApiTest, LibraryAndSymbolFailuresCanBeRetried) {
  int attempts = 0;
  int resolutions = 0;
  int unloads = 0;
  const dyn::loader_t loader {
    [&](const std::vector<const char *> &names) -> std::shared_ptr<dyn::library_handle_t> {
      EXPECT_STREQ(names[0], "libnvidia-fbc.so.1");
      EXPECT_STREQ(names[1], "libnvidia-fbc.so");
      if (++attempts == 1) {
        return {};
      }
      return make_fake_library(1, [&]() {
        ++unloads;
      });
    },
    [&](dyn::library_handle_t *, const dyn::symbols_t &symbols) {
      if (++resolutions == 1) {
        return -1;
      }
      EXPECT_STREQ(std::get<1>(symbols.front()), "NvFBCCreateInstance");
      *std::get<0>(symbols.front()) = reinterpret_cast<dyn::apiproc>(&fake_create_instance<>);
      return 0;
    },
  };
  EXPECT_FALSE(cuda::nvfbc::api_t::load(loader));
  EXPECT_EQ(resolutions, 0);
  EXPECT_FALSE(cuda::nvfbc::api_t::load(loader));
  EXPECT_EQ(unloads, 1);
  auto api = cuda::nvfbc::api_t::load(loader);
  ASSERT_TRUE(api);
  EXPECT_EQ(api->functions.nvFBCGetLastErrorStr(1), std::string_view("fake driver"));
  EXPECT_EQ(unloads, 1);
  api.reset();
  EXPECT_EQ(unloads, 2);
}

TEST(NvfbcApiTest, ApiCreationFailureReleasesTheLibrary) {
  bool unloaded = false;
  const dyn::loader_t loader {
    [&](const std::vector<const char *> &) {
      return make_fake_library(1, [&]() {
        unloaded = true;
      });
    },
    [](dyn::library_handle_t *, const dyn::symbols_t &symbols) {
      *std::get<0>(symbols.front()) = reinterpret_cast<dyn::apiproc>(&fake_create_instance<-1, NVFBC_ERR_API_VERSION>);
      return 0;
    },
  };
  EXPECT_FALSE(cuda::nvfbc::api_t::load(loader));
  EXPECT_TRUE(unloaded);
}

TEST_P(NvfbcIncompleteApiTest, MissingRequiredFunctionReleasesTheLibrary) {
  bool unloaded = false;
  const dyn::loader_t loader {
    [&](const std::vector<const char *> &) {
      return make_fake_library(1, [&]() {
        unloaded = true;
      });
    },
    [&](dyn::library_handle_t *, const dyn::symbols_t &symbols) {
      *std::get<0>(symbols.front()) = reinterpret_cast<dyn::apiproc>(GetParam());
      return 0;
    },
  };
  EXPECT_FALSE(cuda::nvfbc::api_t::load(loader));
  EXPECT_TRUE(unloaded);
}

INSTANTIATE_TEST_SUITE_P(RequiredFunctions, NvfbcIncompleteApiTest, ::testing::Values(&fake_create_instance<0>, &fake_create_instance<1>, &fake_create_instance<2>, &fake_create_instance<3>, &fake_create_instance<4>, &fake_create_instance<5>, &fake_create_instance<6>, &fake_create_instance<7>, &fake_create_instance<8>, &fake_create_instance<9>));

#endif
