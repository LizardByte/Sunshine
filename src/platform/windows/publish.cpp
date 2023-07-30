/**
 * @file src/platform/windows/publish.cpp
 * @brief todo
 */
#include <winsock2.h>

#include <windows.h>

#include <windns.h>
#include <winerror.h>

#include <boost/asio/ip/host_name.hpp>

#include <codecvt>

#include "misc.h"
#include "src/config.h"
#include "src/main.h"
#include "src/network.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include "src/thread_safe.h"

#define _FN(x, ret, args)    \
  typedef ret(*x##_fn) args; \
  static x##_fn x

using namespace std::literals;

#define __SV(quote) L##quote##sv
#define SV(quote) __SV(quote)

extern "C" {

#define SERVICE_DOMAIN "local"

constexpr auto SERVICE_INSTANCE_NAME = SV(SERVICE_NAME "." SERVICE_TYPE "." SERVICE_DOMAIN);
constexpr auto SERVICE_TYPE_DOMAIN = SV(SERVICE_TYPE "." SERVICE_DOMAIN);

typedef VOID WINAPI
DNS_SERVICE_REGISTER_COMPLETE(
  _In_ DWORD Status,
  _In_ PVOID pQueryContext,
  _In_ PDNS_SERVICE_INSTANCE pInstance);

typedef DNS_SERVICE_REGISTER_COMPLETE *PDNS_SERVICE_REGISTER_COMPLETE;

_FN(_DnsServiceFreeInstance, VOID, (_In_ PDNS_SERVICE_INSTANCE pInstance));
_FN(_DnsServiceDeRegister, DWORD, (_In_ PDNS_SERVICE_REGISTER_REQUEST pRequest, _Inout_opt_ PDNS_SERVICE_CANCEL pCancel));
_FN(_DnsServiceRegister, DWORD, (_In_ PDNS_SERVICE_REGISTER_REQUEST pRequest, _Inout_opt_ PDNS_SERVICE_CANCEL pCancel));
} /* extern "C" */

namespace platf::publish {
  VOID WINAPI
  register_cb(DWORD status, PVOID pQueryContext, PDNS_SERVICE_INSTANCE pInstance) {
    auto alarm = (safe::alarm_t<PDNS_SERVICE_INSTANCE>::element_type *) pQueryContext;

    if (status) {
      print_status("register_cb()"sv, status);
    }

    alarm->ring(pInstance);
  }

  static int
  service(bool enable, PDNS_SERVICE_INSTANCE &existing_instance) {
    auto alarm = safe::make_alarm<PDNS_SERVICE_INSTANCE>();

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;

    std::wstring name { SERVICE_INSTANCE_NAME.data(), SERVICE_INSTANCE_NAME.size() };
    std::wstring domain { SERVICE_TYPE_DOMAIN.data(), SERVICE_TYPE_DOMAIN.size() };

    auto host = converter.from_bytes(boost::asio::ip::host_name() + ".local");

    DNS_SERVICE_INSTANCE instance {};
    instance.pszInstanceName = name.data();
    instance.wPort = map_port(nvhttp::PORT_HTTP);
    instance.pszHostName = host.data();

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
    }
    else {
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
    }
    else if (registered_instance) {
      // Deregistration was successful
      _DnsServiceFreeInstance(registered_instance);
      existing_instance = nullptr;
    }

    return registered_instance ? 0 : -1;
  }

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

  int
  load_funcs(HMODULE handle) {
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

  std::unique_ptr<::platf::deinit_t>
  start() {
    HMODULE handle = LoadLibrary("dnsapi.dll");

    if (!handle || load_funcs(handle)) {
      BOOST_LOG(error) << "Couldn't load dnsapi.dll, You'll need to add PC manually from Moonlight"sv;
      return nullptr;
    }

    return std::make_unique<mdns_registration_t>();
  }
}  // namespace platf::publish
