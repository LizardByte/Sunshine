#include <winsock2.h>

#include <windows.h>

#include <windns.h>
#include <winerror.h>

#include <boost/asio/ip/host_name.hpp>

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
#ifndef __MINGW32__
constexpr auto DNS_REQUEST_PENDING        = 9506L;
constexpr auto DNS_QUERY_REQUEST_VERSION1 = 0x1;
constexpr auto DNS_QUERY_RESULTS_VERSION1 = 0x1;
#endif

#define SERVICE_DOMAIN "local"

constexpr auto SERVICE_INSTANCE_NAME = SV(SERVICE_NAME "." SERVICE_TYPE "." SERVICE_DOMAIN);
constexpr auto SERVICE_TYPE_DOMAIN   = SV(SERVICE_TYPE "." SERVICE_DOMAIN);

#ifndef __MINGW32__
typedef struct _DNS_SERVICE_INSTANCE {
  LPWSTR pszInstanceName;
  LPWSTR pszHostName;

  IP4_ADDRESS *ip4Address;
  IP6_ADDRESS *ip6Address;

  WORD wPort;
  WORD wPriority;
  WORD wWeight;

  // Property list
  DWORD dwPropertyCount;

  PWSTR *keys;
  PWSTR *values;

  DWORD dwInterfaceIndex;
} DNS_SERVICE_INSTANCE, *PDNS_SERVICE_INSTANCE;
#endif

typedef VOID WINAPI DNS_SERVICE_REGISTER_COMPLETE(
  _In_ DWORD Status,
  _In_ PVOID pQueryContext,
  _In_ PDNS_SERVICE_INSTANCE pInstance);

typedef DNS_SERVICE_REGISTER_COMPLETE *PDNS_SERVICE_REGISTER_COMPLETE;

#ifndef __MINGW32__
typedef struct _DNS_SERVICE_CANCEL {
  PVOID reserved;
} DNS_SERVICE_CANCEL, *PDNS_SERVICE_CANCEL;

typedef struct _DNS_SERVICE_REGISTER_REQUEST {
  ULONG Version;
  ULONG InterfaceIndex;
  PDNS_SERVICE_INSTANCE pServiceInstance;
  PDNS_SERVICE_REGISTER_COMPLETE pRegisterCompletionCallback;
  PVOID pQueryContext;
  HANDLE hCredentials;
  BOOL unicastEnabled;
} DNS_SERVICE_REGISTER_REQUEST, *PDNS_SERVICE_REGISTER_REQUEST;
#endif

_FN(_DnsServiceFreeInstance, VOID, (_In_ PDNS_SERVICE_INSTANCE pInstance));
_FN(_DnsServiceDeRegister, DWORD, (_In_ PDNS_SERVICE_REGISTER_REQUEST pRequest, _Inout_opt_ PDNS_SERVICE_CANCEL pCancel));
_FN(_DnsServiceRegister, DWORD, (_In_ PDNS_SERVICE_REGISTER_REQUEST pRequest, _Inout_opt_ PDNS_SERVICE_CANCEL pCancel));
} /* extern "C" */

namespace platf::publish {
VOID WINAPI register_cb(DWORD status, PVOID pQueryContext, PDNS_SERVICE_INSTANCE pInstance) {
  auto alarm = (safe::alarm_t<DNS_STATUS>::element_type *)pQueryContext;

  auto fg = util::fail_guard([&]() {
    if(pInstance) {
      _DnsServiceFreeInstance(pInstance);
    }
  });

  if(status) {
    print_status("register_cb()"sv, status);
    alarm->ring(-1);

    return;
  }

  alarm->ring(0);
}

static int service(bool enable) {
  auto alarm = safe::make_alarm<DNS_STATUS>();

  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;

  std::wstring name { SERVICE_INSTANCE_NAME.data(), SERVICE_INSTANCE_NAME.size() };
  std::wstring domain { SERVICE_TYPE_DOMAIN.data(), SERVICE_TYPE_DOMAIN.size() };

  auto host = converter.from_bytes(boost::asio::ip::host_name() + ".local");

  DNS_SERVICE_INSTANCE instance {};
  instance.pszInstanceName = name.data();
  instance.wPort           = map_port(nvhttp::PORT_HTTP);
  instance.pszHostName     = host.data();

  DNS_SERVICE_REGISTER_REQUEST req {};
  req.Version                     = DNS_QUERY_REQUEST_VERSION1;
  req.pQueryContext               = alarm.get();
  req.pServiceInstance            = &instance;
  req.pRegisterCompletionCallback = register_cb;

  DNS_STATUS status {};

  if(enable) {
    status = _DnsServiceRegister(&req, nullptr);
  }
  else {
    status = _DnsServiceDeRegister(&req, nullptr);
  }

  alarm->wait();

  status = *alarm->status();
  if(status) {
    BOOST_LOG(error) << "No mDNS service"sv;
    return -1;
  }

  return 0;
}

class deinit_t : public ::platf::deinit_t {
public:
  ~deinit_t() override {
    if(service(false)) {
      std::abort();
    }

    BOOST_LOG(info) << "Unregistered Sunshine Gamestream service"sv;
  }
};

int load_funcs(HMODULE handle) {
  auto fg = util::fail_guard([handle]() {
    FreeLibrary(handle);
  });

  _DnsServiceFreeInstance = (_DnsServiceFreeInstance_fn)GetProcAddress(handle, "DnsServiceFreeInstance");
  _DnsServiceDeRegister   = (_DnsServiceDeRegister_fn)GetProcAddress(handle, "DnsServiceDeRegister");
  _DnsServiceRegister     = (_DnsServiceRegister_fn)GetProcAddress(handle, "DnsServiceRegister");

  if(!(_DnsServiceFreeInstance && _DnsServiceDeRegister && _DnsServiceRegister)) {
    BOOST_LOG(error) << "mDNS service not available in dnsapi.dll"sv;
    return -1;
  }

  fg.disable();
  return 0;
}

std::unique_ptr<::platf::deinit_t> start() {
  HMODULE handle = LoadLibrary("dnsapi.dll");

  if(!handle || load_funcs(handle)) {
    BOOST_LOG(error) << "Couldn't load dnsapi.dll, You'll need to add PC manually from Moonlight"sv;
    return nullptr;
  }

  if(service(true)) {
    return nullptr;
  }

  BOOST_LOG(info) << "Registered Sunshine Gamestream service"sv;

  return std::make_unique<deinit_t>();
}
} // namespace platf::publish
