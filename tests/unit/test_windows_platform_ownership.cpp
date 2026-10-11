/**
 * @file tests/unit/test_windows_platform_ownership.cpp
 * @brief Test optional networking and Windows service ownership without native services.
 */
#include "../tests_common.h"
#ifdef _WIN32
  #include <src/platform/windows/streaming_api.h>
  #include <tools/service_state.h>

namespace {
  /**
   * @brief Fake QWAVE client with an observable cleanup operation.
   */
  struct fake_qos_t {
    std::function<void()> close;  ///< Test cleanup observer.
  };

  /**
   * @brief Create an isolated fake client.
   * @param version Requested version.
   * @param handle New client.
   * @return Success.
   */
  BOOL WINAPI create_qos(PQOS_VERSION version, PHANDLE handle) {
    *handle = new fake_qos_t {};
    return TRUE;
  }

  /**
   * @brief Reject client creation.
   * @param version Requested version.
   * @param handle Unmodified client.
   * @return Failure.
   */
  BOOL WINAPI reject_qos(PQOS_VERSION version, PHANDLE handle) {
    return FALSE;
  }

  /**
   * @brief Close a fake client.
   * @param handle Client.
   * @return Success.
   */
  BOOL WINAPI close_qos(HANDLE handle) {
    auto client = static_cast<fake_qos_t *>(handle);
    if (client->close) {
      client->close();
    }
    delete client;
    return TRUE;
  }

  /**
   * @brief Stand-in export used only for immutable table validation.
   */
  void WINAPI unused_export() {}

  /**
   * @brief Resolve complete test exports.
   * @param name Export name.
   * @return Function address.
   */
  void *complete_export(const char *name) {
    if (std::string_view(name) == "QOSCreateHandle") {
      return reinterpret_cast<void *>(&create_qos);
    }
    if (std::string_view(name) == "QOSCloseHandle") {
      return reinterpret_cast<void *>(&close_qos);
    }
    return reinterpret_cast<void *>(&unused_export);
  }

  /**
   * @brief Create an injectable test library.
   * @param resolve Export lookup.
   * @param unload Unload observer.
   * @return Test loader.
   */
  platf::networking_loader_t loader(std::function<void *(void *, const char *)> resolve = [](void *, const char *name) {
    return complete_export(name);
  },
                                    std::function<void()> unload = []() {
                                    }) {
    return {
      [unload](const char *) {
        return std::shared_ptr<void>(new int {}, [unload](void *library) {
          delete static_cast<int *>(library);
          unload();
        });
      },
      std::move(resolve),
    };
  }
}  // namespace

TEST(WindowsNetworkingOwnerTest, MissingLibrariesSkipSymbolLookup) {
  int resolutions = 0;
  platf::networking_loader_t missing {[](const char *) {
                                        return std::shared_ptr<void> {};
                                      },
                                      [&resolutions](void *, const char *) -> void * {
                                        ++resolutions;
                                        return nullptr;
                                      }};
  EXPECT_FALSE(platf::wlan_api_t::load(missing));
  EXPECT_FALSE(platf::qwave_api_t::load(missing));
  EXPECT_EQ(resolutions, 0);
}

TEST(WindowsNetworkingOwnerTest, EveryMissingWlanExportDiscardsLibraryAndCanRetry) {
  for (const char *missing : {"WlanOpenHandle", "WlanCloseHandle", "WlanFreeMemory", "WlanEnumInterfaces", "WlanSetInterface"}) {
    int unloaded = 0;
    auto partial = loader([missing](void *, const char *name) {
      return std::string_view(name) == missing ? nullptr : complete_export(name);
    },
                          [&unloaded]() {
                            ++unloaded;
                          });
    EXPECT_FALSE(platf::wlan_api_t::load(partial)) << missing;
    EXPECT_EQ(unloaded, 1);
    EXPECT_TRUE(platf::wlan_api_t::load(loader()));
  }
}

TEST(WindowsNetworkingOwnerTest, CompleteWlanTableRetainsLibraryUntilLastResource) {
  int unloaded = 0;
  auto api = platf::wlan_api_t::load(loader([](void *, const char *name) {
    return complete_export(name);
  },
                                            [&unloaded]() {
                                              ++unloaded;
                                            }));
  ASSERT_TRUE(api);
  auto resource = api;
  api.reset();
  EXPECT_EQ(unloaded, 0);
  resource.reset();
  EXPECT_EQ(unloaded, 1);
}

TEST(WindowsNetworkingOwnerTest, EveryMissingQwaveExportDiscardsLibraryAndCanRetry) {
  for (const char *missing : {"QOSCreateHandle", "QOSCloseHandle", "QOSAddSocketToFlow", "QOSRemoveSocketFromFlow"}) {
    int unloaded = 0;
    auto partial = loader([missing](void *, const char *name) {
      return std::string_view(name) == missing ? nullptr : complete_export(name);
    },
                          [&unloaded]() {
                            ++unloaded;
                          });
    EXPECT_FALSE(platf::qwave_api_t::load(partial)) << missing;
    EXPECT_EQ(unloaded, 1);
    EXPECT_TRUE(platf::qwave_api_t::load(loader()));
  }
}

TEST(WindowsNetworkingOwnerTest, FailedClientCreationReleasesLibraryWithoutClosingClient) {
  int unloaded = 0;
  auto rejected = loader([](void *, const char *name) {
    return std::string_view(name) == "QOSCreateHandle" ? reinterpret_cast<void *>(&reject_qos) : complete_export(name);
  },
                         [&unloaded]() {
                           ++unloaded;
                         });
  EXPECT_FALSE(platf::qwave_api_t::load(rejected));
  EXPECT_EQ(unloaded, 1);
  EXPECT_TRUE(platf::qwave_api_t::load(loader()));
}

TEST(WindowsNetworkingOwnerTest, LastFlowClosesClientBeforeLibraryUnload) {
  std::vector<std::string> events;
  auto api = platf::qwave_api_t::load(loader([](void *, const char *name) {
    return complete_export(name);
  },
                                             [&events]() {
                                               events.emplace_back("library");
                                             }));
  ASSERT_TRUE(api);
  static_cast<fake_qos_t *>(api->handle)->close = [&events]() {
    events.emplace_back("client");
  };
  auto flow = api;
  api.reset();
  EXPECT_TRUE(events.empty());
  flow.reset();
  EXPECT_EQ(events, (std::vector<std::string> {"client", "library"}));
}

TEST(WindowsServiceOwnerTest, StopReportsDeadlineBeforeSignalingOnlyItsOwner) {
  std::vector<std::string> events;
  SERVICE_STATUS snapshot {};
  service::state_t first {{[&snapshot, &events](SERVICE_STATUS_HANDLE handle, LPSERVICE_STATUS state) {
                             snapshot = *state;
                             events.emplace_back("status");
                             return TRUE;
                           },
                           [&events](HANDLE handle) {
                             EXPECT_EQ(handle, reinterpret_cast<HANDLE>(1));
                             events.emplace_back("stop");
                             return TRUE;
                           },
                           [&events](HANDLE handle) {
                             events.emplace_back("close");
                             return TRUE;
                           }}};
  first.stop_event = reinterpret_cast<HANDLE>(1);
  EXPECT_EQ(first.control(SERVICE_CONTROL_STOP, 0), NO_ERROR);
  EXPECT_EQ(snapshot.dwCurrentState, SERVICE_STOP_PENDING);
  EXPECT_EQ(snapshot.dwControlsAccepted, 0);
  EXPECT_EQ(snapshot.dwWaitHint, 30000);
  EXPECT_EQ(events, (std::vector<std::string> {"status", "stop"}));
}

TEST(WindowsServiceOwnerTest, SessionControlsSignalOnlyConsoleConnectAndUnsupportedControlFails) {
  int signals = 0;
  service::state_t state {{[](SERVICE_STATUS_HANDLE, LPSERVICE_STATUS) {
                             return TRUE;
                           },
                           [&signals](HANDLE handle) {
                             EXPECT_EQ(handle, reinterpret_cast<HANDLE>(2));
                             ++signals;
                             return TRUE;
                           },
                           [](HANDLE) {
                             return TRUE;
                           }}};
  state.session_change_event = reinterpret_cast<HANDLE>(2);
  EXPECT_EQ(state.control(SERVICE_CONTROL_INTERROGATE, 0), NO_ERROR);
  EXPECT_EQ(state.control(SERVICE_CONTROL_SESSIONCHANGE, WTS_CONSOLE_DISCONNECT), NO_ERROR);
  EXPECT_EQ(signals, 0);
  EXPECT_EQ(state.control(SERVICE_CONTROL_SESSIONCHANGE, WTS_CONSOLE_CONNECT), NO_ERROR);
  EXPECT_EQ(signals, 1);
  EXPECT_EQ(state.control(0xffffffff, 0), ERROR_CALL_NOT_IMPLEMENTED);
}

TEST(WindowsServiceOwnerTest, PartialStartupAndRepeatedInstancesReleaseOnlyOwnedEvents) {
  std::vector<HANDLE> closed;
  auto operations = service::operations_t {[](SERVICE_STATUS_HANDLE, LPSERVICE_STATUS) {
                                             return TRUE;
                                           },
                                           [](HANDLE) {
                                             return TRUE;
                                           },
                                           [&closed](HANDLE handle) {
                                             closed.emplace_back(handle);
                                             return TRUE;
                                           }};
  {
    service::state_t empty {operations};
    EXPECT_EQ(empty.control(SERVICE_CONTROL_PRESHUTDOWN, 0), NO_ERROR);
    EXPECT_EQ(empty.control(SERVICE_CONTROL_SESSIONCHANGE, WTS_CONSOLE_CONNECT), NO_ERROR);
  }
  EXPECT_TRUE(closed.empty());
  {
    service::state_t partial {operations};
    partial.stop_event = reinterpret_cast<HANDLE>(1);
  }
  EXPECT_EQ(closed, (std::vector<HANDLE> {reinterpret_cast<HANDLE>(1)}));
  {
    service::state_t full {operations};
    full.stop_event = reinterpret_cast<HANDLE>(3);
    full.session_change_event = reinterpret_cast<HANDLE>(4);
    full.report(SERVICE_STOPPED, 0, ERROR_ACCESS_DENIED);
  }
  EXPECT_EQ(closed, (std::vector<HANDLE> {reinterpret_cast<HANDLE>(1), reinterpret_cast<HANDLE>(4), reinterpret_cast<HANDLE>(3)}));
}
#endif
