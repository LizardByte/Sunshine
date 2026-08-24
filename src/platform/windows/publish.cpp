/**
 * @file src/platform/windows/publish.cpp
 * @brief Definitions for Windows mDNS service registration.
 */
// platform includes
// WinSock2.h must be included before Windows.h
// clang-format off
#include <WinSock2.h>
#include <Windows.h>
// clang-format on
#include <WinDNS.h>
#include <winerror.h>

// local includes
#include "misc.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/network.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include "src/thread_safe.h"
#include "utf_utils.h"

/**
 * @def _FN(x, ret, args)
 * @brief Macro for FN.
 */
#define _FN(x, ret, args) \
  /** \
   * @brief Function pointer type for the dynamically loaded DNS-SD entry point. \
   */ \
  typedef ret(*x##_fn) args; \
  /** \
   * @brief Loaded DNS-SD entry point pointer. \
   */ \
  static x##_fn x

using namespace std::literals;

/**
 * @def __SV(quote)
 * @brief Macro for SV.
 */
#define __SV(quote) L##quote##sv
/**
 * @def SV(quote)
 * @brief Macro for SV.
 */
#define SV(quote) __SV(quote)

extern "C" {
#ifndef __MINGW32__
  constexpr auto DNS_REQUEST_PENDING = 9506L;  ///< Windows DNS API constant for request pending.
  constexpr auto DNS_QUERY_REQUEST_VERSION1 = 0x1;  ///< Windows DNS API constant for query request version1.
  constexpr auto DNS_QUERY_RESULTS_VERSION1 = 0x1;  ///< Windows DNS API constant for query results version1.
#endif

  constexpr auto SERVICE_DOMAIN = "local";  ///< Protocol or platform constant for service domain.
  const auto SERVICE_TYPE_DOMAIN = std::format("{}.{}"sv, platf::SERVICE_TYPE, SERVICE_DOMAIN);  ///< Protocol or platform constant for service type domain.

#ifndef __MINGW32__
  /**
   * @brief Windows DNS-SD service instance registration data.
   */
  typedef struct _DNS_SERVICE_INSTANCE {
    LPWSTR pszInstanceName;  ///< DNS-SD service instance name.
    LPWSTR pszHostName;  ///< Hostname advertising the DNS-SD service.

    IP4_ADDRESS *ip4Address;  ///< Optional IPv4 address advertised with the service.
    IP6_ADDRESS *ip6Address;  ///< Optional IPv6 address advertised with the service.

    WORD wPort;  ///< TCP or UDP port advertised for the service.
    WORD wPriority;  ///< DNS-SD priority value.
    WORD wWeight;  ///< DNS-SD weight value.

    // Property list
    DWORD dwPropertyCount;  ///< Number of TXT record key/value pairs.

    PWSTR *keys;  ///< DNS TXT record keys.
    PWSTR *values;  ///< DNS TXT record values paired with `keys`.

    DWORD dwInterfaceIndex;  ///< Network interface index used for registration.
  } DNS_SERVICE_INSTANCE, *PDNS_SERVICE_INSTANCE;  ///< Alias for DNS SERVICE INSTANCE.
#endif

  /**
   * @brief DNS service registration completion callback.
   *
   * @param Status Registration status.
   * @param pQueryContext User query context.
   * @param pInstance DNS service instance.
   */
  typedef VOID WINAPI
    DNS_SERVICE_REGISTER_COMPLETE(
      _In_ DWORD Status,
      _In_ PVOID pQueryContext,
      _In_ PDNS_SERVICE_INSTANCE pInstance
    );

  /**
   * @brief Pointer to the Windows DNS-SD registration completion callback.
   */
  typedef DNS_SERVICE_REGISTER_COMPLETE *PDNS_SERVICE_REGISTER_COMPLETE;

#ifndef __MINGW32__
  /**
   * @brief Windows DNS-SD cancellation request data.
   */
  typedef struct _DNS_SERVICE_CANCEL {
    PVOID reserved;  ///< Reserved by the Windows DNS-SD API and left null.
  } DNS_SERVICE_CANCEL, *PDNS_SERVICE_CANCEL;  ///< Alias for DNS SERVICE CANCEL.

  /**
   * @brief Windows DNS-SD service registration request data.
   */
  typedef struct _DNS_SERVICE_REGISTER_REQUEST {
    ULONG Version;  ///< Windows DNS-SD request structure version.
    ULONG InterfaceIndex;  ///< Network interface index used for registration.
    PDNS_SERVICE_INSTANCE pServiceInstance;  ///< Service instance being registered.
    PDNS_SERVICE_REGISTER_COMPLETE pRegisterCompletionCallback;  ///< Callback invoked when registration completes.
    PVOID pQueryContext;  ///< Caller-provided context passed to the completion callback.
    HANDLE hCredentials;  ///< Optional credentials handle supplied to Windows DNS-SD.
    BOOL unicastEnabled;  ///< Whether the DNS-SD registration is unicast-only.
  } DNS_SERVICE_REGISTER_REQUEST, *PDNS_SERVICE_REGISTER_REQUEST;  ///< Alias for DNS SERVICE REGISTER REQUEST.
#endif

  _FN(_DnsServiceFreeInstance, VOID, (_In_ PDNS_SERVICE_INSTANCE pInstance));
  _FN(_DnsServiceDeRegister, DWORD, (_In_ PDNS_SERVICE_REGISTER_REQUEST pRequest, _Inout_opt_ PDNS_SERVICE_CANCEL pCancel));
  _FN(_DnsServiceRegister, DWORD, (_In_ PDNS_SERVICE_REGISTER_REQUEST pRequest, _Inout_opt_ PDNS_SERVICE_CANCEL pCancel));
} /* extern "C" */

namespace platf::publish {
  /**
   * @brief Handle completion of a Windows DNS-SD registration request.
   *
   * @param status Native status code returned by the platform API.
   * @param pQueryContext Alarm object signaled when registration completes.
   * @param pInstance Registered DNS-SD service instance returned by Windows.
   * @return Callback has no return value; completion is reported through the alarm.
   */
  VOID WINAPI register_cb(DWORD status, PVOID pQueryContext, PDNS_SERVICE_INSTANCE pInstance) {
    auto alarm = (safe::alarm_t<PDNS_SERVICE_INSTANCE>::element_type *) pQueryContext;

    if (status) {
      print_status("register_cb()"sv, status);
    }

    alarm->ring(pInstance);
  }

  static int service(bool enable, PDNS_SERVICE_INSTANCE &existing_instance) {
    auto alarm = safe::make_alarm<PDNS_SERVICE_INSTANCE>();

    std::wstring domain = utf_utils::from_utf8(SERVICE_TYPE_DOMAIN);

    auto hostname = platf::get_host_name();
    auto name = utf_utils::from_utf8(net::mdns_instance_name(hostname) + '.') + domain;
    auto host = utf_utils::from_utf8(hostname + ".local");

    DNS_SERVICE_INSTANCE instance {};
    instance.pszInstanceName = name.data();
    instance.wPort = net::map_port(nvhttp::PORT_HTTP);
    instance.pszHostName = host.data();

    // Setting these values ensures Windows mDNS answers comply with RFC 1035.
    // If these are unset, Windows will send a TXT record that has zero strings,
    // which is illegal. Setting them to a single empty value causes Windows to
    // send a single empty string for the TXT record, which is the correct thing
    // to do when advertising a service without any TXT strings.
    //
    // Most clients aren't strictly checking TXT record compliance with RFC 1035,
    // but Apple's mDNS resolver does and rejects the entire answer if an invalid
    // TXT record is present.
    PWCHAR keys[] = {nullptr};
    PWCHAR values[] = {nullptr};
    instance.dwPropertyCount = 1;
    instance.keys = keys;
    instance.values = values;

    DNS_SERVICE_REGISTER_REQUEST req {};
    req.Version = DNS_QUERY_REQUEST_VERSION1;
    req.pQueryContext = alarm.get();
    req.pServiceInstance = enable ? &instance : existing_instance;
    req.pRegisterCompletionCallback = register_cb;

    DNS_STATUS status {};

    if (enable) {
      status = _DnsServiceRegister(&req, nullptr);
      if (status != DNS_REQUEST_PENDING) {
        print_status("DnsServiceRegister()"sv, status);
        return -1;
      }
    } else {
      status = _DnsServiceDeRegister(&req, nullptr);
      if (status != DNS_REQUEST_PENDING) {
        print_status("DnsServiceDeRegister()"sv, status);
        return -1;
      }
    }

    alarm->wait();

    auto registered_instance = alarm->status();
    if (enable) {
      // Store this instance for later deregistration
      existing_instance = registered_instance;
    } else if (registered_instance) {
      // Deregistration was successful
      _DnsServiceFreeInstance(registered_instance);
      existing_instance = nullptr;
    }

    return registered_instance ? 0 : -1;
  }

  /**
   * @brief Windows DNS-SD registration lifetime for the advertised Sunshine service.
   */
  class mdns_registration_t: public ::platf::deinit_t {
  public:
    mdns_registration_t():
        existing_instance(nullptr) {
      if (service(true, existing_instance)) {
        BOOST_LOG(error) << "Unable to register Sunshine mDNS service"sv;
        return;
      }

      BOOST_LOG(info) << "Registered Sunshine mDNS service"sv;
    }

    ~mdns_registration_t() override {
      if (existing_instance) {
        if (service(false, existing_instance)) {
          BOOST_LOG(error) << "Unable to unregister Sunshine mDNS service"sv;
          return;
        }

        BOOST_LOG(info) << "Unregistered Sunshine mDNS service"sv;
      }
    }

  private:
    PDNS_SERVICE_INSTANCE existing_instance;
  };

  /**
   * @brief Resolve required function pointers from the native library.
   *
   * @param handle Native library or object handle used by the operation.
   * @return 0 when all required DNS-SD functions are resolved; nonzero otherwise.
   */
  int load_funcs(HMODULE handle) {
    auto fg = util::fail_guard([handle]() {
      FreeLibrary(handle);
    });

    _DnsServiceFreeInstance = (_DnsServiceFreeInstance_fn) GetProcAddress(handle, "DnsServiceFreeInstance");
    _DnsServiceDeRegister = (_DnsServiceDeRegister_fn) GetProcAddress(handle, "DnsServiceDeRegister");
    _DnsServiceRegister = (_DnsServiceRegister_fn) GetProcAddress(handle, "DnsServiceRegister");

    if (!(_DnsServiceFreeInstance && _DnsServiceDeRegister && _DnsServiceRegister)) {
      BOOST_LOG(error) << "mDNS service not available in dnsapi.dll"sv;
      return -1;
    }

    fg.disable();
    return 0;
  }

  std::unique_ptr<::platf::deinit_t> start() {
    HMODULE handle = LoadLibrary("dnsapi.dll");

    if (!handle || load_funcs(handle)) {
      BOOST_LOG(error) << "Couldn't load dnsapi.dll, You'll need to add PC manually from Moonlight"sv;
      return nullptr;
    }

    return std::make_unique<mdns_registration_t>();
  }
}  // namespace platf::publish
