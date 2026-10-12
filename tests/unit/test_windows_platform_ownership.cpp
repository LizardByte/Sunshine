/**
 * @file tests/unit/test_windows_platform_ownership.cpp
 * @brief Test optional networking and Windows service ownership without native services.
 */
#include "../tests_common.h"
#ifdef _WIN32
  #include <bit>
  #include <cstddef>
  #include <cstdlib>
  #include <cstring>
  #include <src/platform/windows/streaming_api.h>
  #include <src/utility.h>
  #include <tools/service_state.h>

namespace {
  /**
   * @brief Fake QWAVE client with an observable cleanup operation.
   */
  struct fake_qos_t {
    std::function<void()> close;  ///< Test cleanup observer.
    SOCKET socket {};  ///< Socket registered by the restored add export.
    ADDRESS_FAMILY family {};  ///< Destination family passed to the add export.
    QOS_TRAFFIC_TYPE traffic {};  ///< Traffic policy passed to the add export.
    DWORD flags {};  ///< Flow flags passed to the add export.
    QOS_FLOWID flow {};  ///< Flow identity returned by the add export.
    bool removed {};  ///< Whether the restored remove export received that flow.
  };

  /**
   * @brief Fake WLAN client recording calls through the restored table.
   */
  struct fake_wlan_t {
    unsigned int enumerations {};  ///< Number of interface enumerations.
    GUID interface_guid {};  ///< Interface selected by the set export.
    WLAN_INTF_OPCODE opcode {};  ///< Option selected by the set export.
    BOOL enabled {};  ///< Boolean option value passed to the set export.
  };

  /**
   * @brief Open a fake WLAN client with the requested API version.
   * @param version Requested version.
   * @param negotiated Negotiated version output.
   * @param handle Allocated client output.
   * @return Success.
   */
  extern "C" DWORD WINAPI open_wlan(DWORD version, PVOID, PDWORD negotiated, PHANDLE handle) {
    *negotiated = version;
    *handle = new fake_wlan_t {};
    return ERROR_SUCCESS;
  }

  /**
   * @brief Close a fake WLAN client.
   * @param handle Client allocated by open_wlan.
   * @return Success.
   */
  extern "C" DWORD WINAPI close_wlan(HANDLE handle, PVOID) {
    delete static_cast<fake_wlan_t *>(handle);
    return ERROR_SUCCESS;
  }

  /**
   * @brief Release a native-layout fake discovery result.
   * @param memory Result allocated by enumerate_wlan.
   */
  extern "C" VOID WINAPI free_wlan(PVOID memory) {
    std::free(memory);
  }

  /**
   * @brief Enumerate one fake interface through the SDK callback signature.
   * @details The SDK list has a variable-length tail, so allocation includes one full interface entry.
   * @param handle Fake client recording the call.
   * @param interfaces Newly allocated discovery result.
   * @return Success, or an out-of-memory error when native-layout storage cannot be allocated.
   */
  extern "C" DWORD WINAPI enumerate_wlan(HANDLE handle, PVOID, PWLAN_INTERFACE_INFO_LIST *interfaces) {
    ++static_cast<fake_wlan_t *>(handle)->enumerations;
    constexpr auto storage_size = offsetof(WLAN_INTERFACE_INFO_LIST, InterfaceInfo) + sizeof(WLAN_INTERFACE_INFO);
    auto result = static_cast<PWLAN_INTERFACE_INFO_LIST>(std::calloc(1, storage_size));
    *interfaces = result;
    if (!result) {
      return ERROR_NOT_ENOUGH_MEMORY;
    }
    result->dwNumberOfItems = 1;
    result->InterfaceInfo[0].InterfaceGuid.Data1 = 17;
    return ERROR_SUCCESS;
  }

  /**
   * @brief Record a boolean interface option through the SDK callback signature.
   * @param handle Fake client recording the call.
   * @param guid Selected interface.
   * @param opcode Selected option.
   * @param size Option payload length.
   * @param data Boolean option payload.
   * @return Success for a boolean payload, otherwise an invalid-parameter error.
   */
  extern "C" DWORD WINAPI set_wlan(HANDLE handle, const GUID *guid, WLAN_INTF_OPCODE opcode, DWORD size, const PVOID data, PVOID) {
    if (size != sizeof(BOOL)) {
      return ERROR_INVALID_PARAMETER;
    }
    auto client = static_cast<fake_wlan_t *>(handle);
    client->interface_guid = *guid;
    client->opcode = opcode;
    client->enabled = *static_cast<const BOOL *>(data);
    return ERROR_SUCCESS;
  }

  /**
   * @brief Injected WLAN results and ordered native call observations.
   */
  struct fake_streaming_wlan_t {
    DWORD open_result {ERROR_SUCCESS};  ///< Result of opening the client.
    DWORD enumeration_result {ERROR_SUCCESS};  ///< Result of interface discovery.
    DWORD set_result {ERROR_SUCCESS};  ///< Result of setting media streaming mode.
    std::unique_ptr<std::byte, decltype(&std::free)> allocation {nullptr, &std::free};  ///< Owned variable-tail SDK list, prefixed with its cleanup observer.
    PWLAN_INTERFACE_INFO_LIST interfaces {};  ///< Native-layout single-interface discovery result.
    std::vector<std::string> events;  ///< Call and observer order.

    /**
     * @brief Allocate a connected interface and prefix its native result with the cleanup observer.
     * @details The observer pointer is copied as bytes; the trailing SDK result has its required alignment.
     */
    fake_streaming_wlan_t() {
      static_assert(sizeof(this) % alignof(WLAN_INTERFACE_INFO_LIST) == 0);
      constexpr auto result_size = offsetof(WLAN_INTERFACE_INFO_LIST, InterfaceInfo) + sizeof(WLAN_INTERFACE_INFO);
      allocation.reset(static_cast<std::byte *>(std::calloc(1, sizeof(this) + result_size)));
      if (!allocation) {
        throw std::bad_alloc {};
      }
      const auto owner = this;
      std::memcpy(allocation.get(), &owner, sizeof(owner));
      interfaces = static_cast<PWLAN_INTERFACE_INFO_LIST>(static_cast<void *>(allocation.get() + sizeof(owner)));
      interfaces->dwNumberOfItems = 1;
      interfaces->InterfaceInfo[0].InterfaceGuid.Data1 = 23;
      interfaces->InterfaceInfo[0].isState = wlan_interface_state_connected;
    }
  };

  /**
   * @brief Open the stack-owned fake client seeded in the output slot by the test.
   * @param version Requested WLAN version.
   * @param reserved SDK reserved argument.
   * @param negotiated Negotiated WLAN version output.
   * @param handle Output slot containing the test's injected state.
   * @return Injected open status without rewriting the output slot.
   */
  DWORD WINAPI open_streaming_wlan(DWORD version, PVOID reserved, PDWORD negotiated, PHANDLE handle) {
    EXPECT_EQ(version, WLAN_API_MAKE_VERSION(2, 0));
    EXPECT_EQ(reserved, nullptr);
    auto &state = *static_cast<fake_streaming_wlan_t *>(*handle);
    state.events.emplace_back("open");
    *negotiated = version;
    return state.open_result;
  }

  /**
   * @brief Record failed-discovery client cleanup without deleting stack-owned state.
   * @param handle Injected client state.
   * @param reserved SDK reserved argument.
   * @return Success.
   */
  DWORD WINAPI close_streaming_wlan(HANDLE handle, PVOID reserved) {
    EXPECT_EQ(reserved, nullptr);
    static_cast<fake_streaming_wlan_t *>(handle)->events.emplace_back("close");
    return ERROR_SUCCESS;
  }

  /**
   * @brief Record discovery-result cleanup through the native base subobject.
   * @param memory Result returned by enumerate_streaming_wlan.
   */
  VOID WINAPI free_streaming_wlan(PVOID memory) {
    fake_streaming_wlan_t *owner;
    std::memcpy(&owner, static_cast<const std::byte *>(memory) - sizeof(owner), sizeof(owner));
    owner->events.emplace_back("free");
    owner->allocation.reset();
  }

  /**
   * @brief Return injected discovery status and a native single-interface result.
   * @param handle Injected client state.
   * @param reserved SDK reserved argument.
   * @param interfaces Discovery output slot.
   * @return Injected enumeration result.
   */
  DWORD WINAPI enumerate_streaming_wlan(HANDLE handle, PVOID reserved, PWLAN_INTERFACE_INFO_LIST *interfaces) {
    EXPECT_EQ(reserved, nullptr);
    auto &state = *static_cast<fake_streaming_wlan_t *>(handle);
    state.events.emplace_back("enumerate");
    *interfaces = state.interfaces;
    return state.enumeration_result;
  }

  /**
   * @brief Check the exact media-streaming request and return an injected status.
   * @param handle Injected client state.
   * @param guid Discovered interface identity.
   * @param opcode Native media streaming option.
   * @param size Boolean payload length.
   * @param data Requested option value.
   * @param reserved SDK reserved argument.
   * @return Injected interface-update result.
   */
  DWORD WINAPI set_streaming_wlan(HANDLE handle, const GUID *guid, WLAN_INTF_OPCODE opcode, DWORD size, const PVOID data, PVOID reserved) {
    auto &state = *static_cast<fake_streaming_wlan_t *>(handle);
    EXPECT_EQ(guid, &state.interfaces->InterfaceInfo[0].InterfaceGuid);
    EXPECT_EQ(opcode, wlan_intf_opcode_media_streaming_mode);
    EXPECT_EQ(size, sizeof(BOOL));
    EXPECT_EQ(*static_cast<const BOOL *>(data), TRUE);
    EXPECT_EQ(reserved, nullptr);
    state.events.emplace_back("set");
    return state.set_result;
  }

  /**
   * @brief Create the exact SDK-signature table for stack-owned streaming tests.
   * @return Injected WLAN table.
   */
  platf::wlan_api_t streaming_wlan_api() {
    return {{}, &open_streaming_wlan, &close_streaming_wlan, &free_streaming_wlan, &enumerate_streaming_wlan, &set_streaming_wlan};
  }

  /**
   * @brief Create an isolated fake client.
   * @details The required native version argument is ignored by this fake.
   * @param handle New client.
   * @return Success.
   */
  BOOL WINAPI create_qos(PQOS_VERSION, PHANDLE handle) {
    *handle = new fake_qos_t {};
    return TRUE;
  }

  /**
   * @brief Reject client creation.
   * @details Native arguments are ignored so the caller's output handle remains unmodified.
   * @return Failure.
   */
  BOOL WINAPI reject_qos(PQOS_VERSION, PHANDLE) {
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
   * @brief Add a fake QWAVE flow through the SDK callback signature.
   * @param handle Fake client recording the call.
   * @param socket Socket assigned to the flow.
   * @param address Destination address.
   * @param traffic Traffic policy.
   * @param flags Flow flags.
   * @param flow Assigned flow identity.
   * @return Success.
   */
  BOOL WINAPI add_qos(HANDLE handle, SOCKET socket, PSOCKADDR address, QOS_TRAFFIC_TYPE traffic, DWORD flags, PQOS_FLOWID flow) {
    auto client = static_cast<fake_qos_t *>(handle);
    client->socket = socket;
    client->family = address->sa_family;
    client->traffic = traffic;
    client->flags = flags;
    client->flow = *flow = 19;
    return TRUE;
  }

  /**
   * @brief Remove the fake QWAVE flow using its exact socket and identity.
   * @param handle Fake client recording the call.
   * @param socket Socket assigned to the flow.
   * @param flow Previously assigned flow identity.
   * @param flags Removal flags.
   * @return Success when the registered flow is matched.
   */
  BOOL WINAPI remove_qos(HANDLE handle, SOCKET socket, QOS_FLOWID flow, DWORD flags) {
    auto client = static_cast<fake_qos_t *>(handle);
    client->removed = client->socket == socket && client->flow == flow && flags == 0;
    return client->removed;
  }

  /**
   * @brief Resolve complete test exports.
   * @param name Export name.
   * @return ABI-erased address of an exact SDK-signature fake, or nullptr for an unknown export.
   */
  FARPROC complete_export(const char *name) {
    if (std::string_view(name) == "QOSCreateHandle") {
      return std::bit_cast<FARPROC>(&create_qos);
    }
    if (std::string_view(name) == "QOSCloseHandle") {
      return std::bit_cast<FARPROC>(&close_qos);
    }
    if (std::string_view(name) == "QOSAddSocketToFlow") {
      return std::bit_cast<FARPROC>(&add_qos);
    }
    if (std::string_view(name) == "QOSRemoveSocketFromFlow") {
      return std::bit_cast<FARPROC>(&remove_qos);
    }
    if (std::string_view(name) == "WlanOpenHandle") {
      return std::bit_cast<FARPROC>(&open_wlan);
    }
    if (std::string_view(name) == "WlanCloseHandle") {
      return std::bit_cast<FARPROC>(&close_wlan);
    }
    if (std::string_view(name) == "WlanFreeMemory") {
      return std::bit_cast<FARPROC>(&free_wlan);
    }
    if (std::string_view(name) == "WlanEnumInterfaces") {
      return std::bit_cast<FARPROC>(&enumerate_wlan);
    }
    if (std::string_view(name) == "WlanSetInterface") {
      return std::bit_cast<FARPROC>(&set_wlan);
    }
    return nullptr;
  }

  /**
   * @brief Create an injectable test library.
   * @param resolve Export lookup.
   * @param unload Unload observer.
   * @return Test loader.
   */
  platf::networking_loader_t loader(std::function<FARPROC(HMODULE, const char *)> resolve = [](HMODULE, const char *name) {
    return complete_export(name);
  },
                                    std::function<void()> unload = []() {
                                    }) {
    return {
      [unload](const char *) {
        return platf::networking_loader_t::library_t {new std::remove_pointer_t<HMODULE> {}, [unload](HMODULE library) {
                                                        delete library;
                                                        unload();
                                                      }};
      },
      std::move(resolve),
    };
  }
}  // namespace

/**
 * @brief The native loader reports missing libraries and exports and resolves a callable system export.
 */
TEST(WindowsNetworkingOwnerTest, SystemLoaderResolvesExportsFromItsOwnedModule) {
  const auto native_loader = platf::system_networking_loader();
  EXPECT_FALSE(native_loader.open("sunshine_missing_networking_api.dll"));
  const auto library = native_loader.open("kernel32.dll");
  ASSERT_TRUE(library);
  EXPECT_EQ(native_loader.resolve(library.get(), "SunshineMissingNetworkingExport"), nullptr);
  const auto address = native_loader.resolve(library.get(), "GetCurrentProcessId");
  ASSERT_NE(address, nullptr);
  const auto get_process_id = std::bit_cast<decltype(GetCurrentProcessId) *>(address);
  EXPECT_EQ(get_process_id(), GetCurrentProcessId());
}

/**
 * @brief Opening failure preserves the native output slot and skips later operations.
 */
TEST(WindowsWlanStreamingTest, OpenFailureSkipsDiscoveryAndCleanup) {
  fake_streaming_wlan_t state;
  state.open_result = ERROR_ACCESS_DENIED;
  HANDLE client = &state;
  streaming_wlan_api().enable_low_latency(client, [&state](DWORD) {
    state.events.emplace_back("enabled");
  });
  EXPECT_EQ(client, &state);
  EXPECT_EQ(state.events, (std::vector<std::string> {"open"}));
}

/**
 * @brief Discovery failure closes the client and clears the owner's handle.
 */
TEST(WindowsWlanStreamingTest, EnumerationFailureClosesAndClearsClient) {
  fake_streaming_wlan_t state;
  state.enumeration_result = ERROR_ACCESS_DENIED;
  HANDLE client = &state;
  streaming_wlan_api().enable_low_latency(client, [&state](DWORD) {
    state.events.emplace_back("enabled");
  });
  EXPECT_EQ(client, nullptr);
  EXPECT_EQ(state.events, (std::vector<std::string> {"open", "enumerate", "close"}));
}

/**
 * @brief An empty discovery result is freed while the successful client remains owned.
 */
TEST(WindowsWlanStreamingTest, EmptyEnumerationFreesResultsAndRetainsClient) {
  fake_streaming_wlan_t state;
  state.interfaces->dwNumberOfItems = 0;
  HANDLE client = &state;
  streaming_wlan_api().enable_low_latency(client, [&state](DWORD) {
    state.events.emplace_back("enabled");
  });
  EXPECT_EQ(client, &state);
  EXPECT_EQ(state.events, (std::vector<std::string> {"open", "enumerate", "free"}));
}

/**
 * @brief Disconnected interfaces do not receive a media streaming request.
 */
TEST(WindowsWlanStreamingTest, DisconnectedInterfaceIsSkipped) {
  fake_streaming_wlan_t state;
  state.interfaces->InterfaceInfo[0].isState = wlan_interface_state_disconnected;
  HANDLE client = &state;
  streaming_wlan_api().enable_low_latency(client, [&state](DWORD) {
    state.events.emplace_back("enabled");
  });
  EXPECT_EQ(client, &state);
  EXPECT_EQ(state.events, (std::vector<std::string> {"open", "enumerate", "free"}));
}

/**
 * @brief An unsuccessful interface update frees discovery results without announcing success.
 */
TEST(WindowsWlanStreamingTest, InterfaceFailureDoesNotNotifyObserver) {
  fake_streaming_wlan_t state;
  state.set_result = ERROR_ACCESS_DENIED;
  HANDLE client = &state;
  streaming_wlan_api().enable_low_latency(client, [&state](DWORD) {
    state.events.emplace_back("enabled");
  });
  EXPECT_EQ(client, &state);
  EXPECT_EQ(state.events, (std::vector<std::string> {"open", "enumerate", "set", "free"}));
}

/**
 * @brief A connected interface receives the exact option before notification and discovery cleanup.
 */
TEST(WindowsWlanStreamingTest, ConnectedInterfaceIsEnabledBeforeResultsAreFreed) {
  fake_streaming_wlan_t state;
  HANDLE client = &state;
  streaming_wlan_api().enable_low_latency(client, [&state](DWORD index) {
    EXPECT_EQ(index, 0);
    state.events.emplace_back("enabled");
  });
  EXPECT_EQ(client, &state);
  EXPECT_EQ(state.events, (std::vector<std::string> {"open", "enumerate", "set", "enabled", "free"}));
}

TEST(WindowsNetworkingOwnerTest, MissingLibrariesSkipSymbolLookup) {
  int resolutions = 0;
  platf::networking_loader_t missing {[](const char *) {
                                        return platf::networking_loader_t::library_t {};
                                      },
                                      [&resolutions](HMODULE, const char *) -> FARPROC {
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
    auto partial = loader([missing](HMODULE, const char *name) {
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
  auto api = platf::wlan_api_t::load(loader([](HMODULE, const char *name) {
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

/**
 * @brief All restored WLAN exports accept and deliver their exact SDK arguments.
 */
TEST(WindowsNetworkingOwnerTest, RestoredWlanExportsAreCallableWithSdkSignatures) {
  auto api = platf::wlan_api_t::load(loader());
  ASSERT_TRUE(api);
  EXPECT_EQ(api->open, &open_wlan);
  EXPECT_EQ(api->close, &close_wlan);
  EXPECT_EQ(api->free_memory, &free_wlan);
  EXPECT_EQ(api->enumerate, &enumerate_wlan);
  EXPECT_EQ(api->set, &set_wlan);

  DWORD negotiated {};
  HANDLE handle {};
  ASSERT_EQ(api->open(2, nullptr, &negotiated, &handle), ERROR_SUCCESS);
  ASSERT_NE(handle, nullptr);
  auto close = util::fail_guard([&api, handle] {
    EXPECT_EQ(api->close(handle, nullptr), ERROR_SUCCESS);
  });
  EXPECT_EQ(negotiated, 2);

  PWLAN_INTERFACE_INFO_LIST interfaces {};
  ASSERT_EQ(api->enumerate(handle, nullptr, &interfaces), ERROR_SUCCESS);
  ASSERT_NE(interfaces, nullptr);
  auto free = util::fail_guard([&api, interfaces] {
    api->free_memory(interfaces);
  });
  EXPECT_EQ(interfaces->dwNumberOfItems, 1);
  EXPECT_EQ(interfaces->dwIndex, 0);
  EXPECT_EQ(interfaces->InterfaceInfo[0].strInterfaceDescription[WLAN_MAX_NAME_LENGTH - 1], L'\0');
  EXPECT_EQ(static_cast<fake_wlan_t *>(handle)->enumerations, 1);

  BOOL enabled = TRUE;
  const auto &guid = interfaces->InterfaceInfo[0].InterfaceGuid;
  EXPECT_EQ(api->set(handle, &guid, wlan_intf_opcode_media_streaming_mode, sizeof(enabled), &enabled, nullptr), ERROR_SUCCESS);
  const auto client = static_cast<fake_wlan_t *>(handle);
  EXPECT_EQ(client->interface_guid.Data1, 17);
  EXPECT_EQ(client->opcode, wlan_intf_opcode_media_streaming_mode);
  EXPECT_EQ(client->enabled, TRUE);
  EXPECT_EQ(api->set(handle, &guid, wlan_intf_opcode_media_streaming_mode, 0, &enabled, nullptr), ERROR_INVALID_PARAMETER);
  EXPECT_EQ(complete_export("UnknownExport"), nullptr);
}

/**
 * @brief Restored QWAVE add and remove exports preserve socket, policy and flow identity.
 */
TEST(WindowsNetworkingOwnerTest, RestoredQwaveExportsAreCallableWithSdkSignatures) {
  auto api = platf::qwave_api_t::load(loader());
  ASSERT_TRUE(api);
  EXPECT_EQ(api->create, &create_qos);
  EXPECT_EQ(api->close, &close_qos);
  EXPECT_EQ(api->add, &add_qos);
  EXPECT_EQ(api->remove, &remove_qos);
  SOCKADDR address {};
  address.sa_family = AF_INET;
  QOS_FLOWID flow {};
  const SOCKET socket = 37;
  ASSERT_TRUE(api->add(api->handle, socket, &address, QOSTrafficTypeAudioVideo, QOS_NON_ADAPTIVE_FLOW, &flow));
  const auto client = static_cast<fake_qos_t *>(api->handle);
  EXPECT_EQ(client->socket, socket);
  EXPECT_EQ(client->family, AF_INET);
  EXPECT_EQ(client->traffic, QOSTrafficTypeAudioVideo);
  EXPECT_EQ(client->flags, QOS_NON_ADAPTIVE_FLOW);
  EXPECT_EQ(flow, 19);
  EXPECT_FALSE(api->remove(api->handle, socket, flow + 1, 0));
  EXPECT_FALSE(client->removed);
  EXPECT_TRUE(api->remove(api->handle, socket, flow, 0));
  EXPECT_TRUE(client->removed);
}

TEST(WindowsNetworkingOwnerTest, EveryMissingQwaveExportDiscardsLibraryAndCanRetry) {
  for (const char *missing : {"QOSCreateHandle", "QOSCloseHandle", "QOSAddSocketToFlow", "QOSRemoveSocketFromFlow"}) {
    int unloaded = 0;
    auto partial = loader([missing](HMODULE, const char *name) {
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
  auto rejected = loader([](HMODULE, const char *name) {
    return std::string_view(name) == "QOSCreateHandle" ? std::bit_cast<FARPROC>(&reject_qos) : complete_export(name);
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
  auto api = platf::qwave_api_t::load(loader([](HMODULE, const char *name) {
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
  int stop_identity {};
  const auto stop_handle = static_cast<HANDLE>(&stop_identity);
  SERVICE_STATUS snapshot {};
  service::state_t first {{[&snapshot, &events](SERVICE_STATUS_HANDLE, const SERVICE_STATUS *state) {
                             snapshot = *state;
                             events.emplace_back("status");
                             return TRUE;
                           },
                           [&events, stop_handle](HANDLE handle) {
                             EXPECT_EQ(handle, stop_handle);
                             events.emplace_back("stop");
                             return TRUE;
                           },
                           [&events](HANDLE) {
                             events.emplace_back("close");
                             return TRUE;
                           }}};
  first.stop_event = stop_handle;
  EXPECT_EQ(first.control(SERVICE_CONTROL_STOP, 0), NO_ERROR);
  EXPECT_EQ(snapshot.dwCurrentState, SERVICE_STOP_PENDING);
  EXPECT_EQ(snapshot.dwControlsAccepted, 0);
  EXPECT_EQ(snapshot.dwWaitHint, 30000);
  EXPECT_EQ(events, (std::vector<std::string> {"status", "stop"}));
}

TEST(WindowsServiceOwnerTest, SessionControlsSignalOnlyConsoleConnectAndUnsupportedControlFails) {
  int signals = 0;
  int session_change_identity {};
  const auto session_change_handle = static_cast<HANDLE>(&session_change_identity);
  service::state_t state {{[](SERVICE_STATUS_HANDLE, LPSERVICE_STATUS) {
                             return TRUE;
                           },
                           [&signals, session_change_handle](HANDLE handle) {
                             EXPECT_EQ(handle, session_change_handle);
                             ++signals;
                             return TRUE;
                           },
                           [](HANDLE) {
                             return TRUE;
                           }}};
  state.session_change_event = session_change_handle;
  EXPECT_EQ(state.control(SERVICE_CONTROL_INTERROGATE, 0), NO_ERROR);
  EXPECT_EQ(state.control(SERVICE_CONTROL_SESSIONCHANGE, WTS_CONSOLE_DISCONNECT), NO_ERROR);
  EXPECT_EQ(signals, 0);
  EXPECT_EQ(state.control(SERVICE_CONTROL_SESSIONCHANGE, WTS_CONSOLE_CONNECT), NO_ERROR);
  EXPECT_EQ(signals, 1);
  EXPECT_EQ(state.control(0xffffffff, 0), ERROR_CALL_NOT_IMPLEMENTED);
}

TEST(WindowsServiceOwnerTest, PartialStartupAndRepeatedInstancesReleaseOnlyOwnedEvents) {
  std::vector<HANDLE> closed;
  int partial_stop_identity {}, full_stop_identity {}, full_session_change_identity {};
  const auto partial_stop_handle = static_cast<HANDLE>(&partial_stop_identity);
  const auto full_stop_handle = static_cast<HANDLE>(&full_stop_identity);
  const auto full_session_change_handle = static_cast<HANDLE>(&full_session_change_identity);
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
    partial.stop_event = partial_stop_handle;
  }
  EXPECT_EQ(closed, (std::vector<HANDLE> {partial_stop_handle}));
  {
    service::state_t full {operations};
    full.stop_event = full_stop_handle;
    full.session_change_event = full_session_change_handle;
    full.report(SERVICE_STOPPED, 0, ERROR_ACCESS_DENIED);
  }
  EXPECT_EQ(closed, (std::vector<HANDLE> {partial_stop_handle, full_session_change_handle, full_stop_handle}));
}
#endif
