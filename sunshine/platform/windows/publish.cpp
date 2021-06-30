#include <winsock2.h>

#include <windows.h>

#include <windns.h>
#include <winerror.h>

#include <boost/asio/ip/host_name.hpp>

#include "sunshine/config.h"
#include "sunshine/main.h"
#include "sunshine/nvhttp.h"
#include "sunshine/platform/common.h"
#include "sunshine/thread_safe.h"

#include "sunshine/network.h"


using namespace std::literals;

#define __SV(quote) L##quote##sv
#define SV(quote) __SV(quote)

extern "C" {
constexpr auto DNS_REQUEST_PENDING        = 9506L;
constexpr auto DNS_QUERY_REQUEST_VERSION1 = 0x1;
constexpr auto DNS_QUERY_RESULTS_VERSION1 = 0x1;

#define SERVICE_DOMAIN "local"

constexpr auto SERVICE_INSTANCE_NAME = SV(SERVICE_NAME "." SERVICE_TYPE "." SERVICE_DOMAIN);
constexpr auto SERVICE_TYPE_DOMAIN   = SV(SERVICE_TYPE "." SERVICE_DOMAIN);

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

typedef VOID WINAPI DNS_SERVICE_REGISTER_COMPLETE(
  _In_ DWORD Status,
  _In_ PVOID pQueryContext,
  _In_ PDNS_SERVICE_INSTANCE pInstance);

typedef DNS_SERVICE_REGISTER_COMPLETE *PDNS_SERVICE_REGISTER_COMPLETE;

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

VOID DnsServiceFreeInstance(
  _In_ PDNS_SERVICE_INSTANCE pInstance);

DWORD DnsServiceDeRegister(
  _In_ PDNS_SERVICE_REGISTER_REQUEST pRequest,
  _Inout_opt_ PDNS_SERVICE_CANCEL pCancel);

DWORD DnsServiceRegister(
  _In_ PDNS_SERVICE_REGISTER_REQUEST pRequest,
  _Inout_opt_ PDNS_SERVICE_CANCEL pCancel);

PDNS_SERVICE_INSTANCE DnsServiceConstructInstance(
  _In_ PCWSTR pServiceName,
  _In_ PCWSTR pHostName,
  _In_opt_ PIP4_ADDRESS pIp4,
  _In_opt_ PIP6_ADDRESS pIp6,
  _In_ WORD wPort,
  _In_ WORD wPriority,
  _In_ WORD wWeight,
  _In_ DWORD dwPropertiesCount,
  _In_reads_(dwPropertiesCount) PCWSTR *keys,
  _In_reads_(dwPropertiesCount) PCWSTR *values);
} /* extern "C" */

void print_status(const std::string_view &prefix, HRESULT status) {
  char err_string[1024];

  DWORD bytes = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    nullptr,
    status,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    err_string,
    sizeof(err_string),
    nullptr);

  BOOST_LOG(error) << prefix << ": "sv << std::string_view { err_string, bytes };
}

namespace platf::publish {
VOID WINAPI register_cb(DWORD status, PVOID pQueryContext, PDNS_SERVICE_INSTANCE pInstance) {
  auto alarm = (safe::alarm_t<DNS_STATUS>::element_type *)pQueryContext;

  auto fg = util::fail_guard([&]() {
    if(pInstance) {
      DnsServiceFreeInstance(pInstance);
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
    status = DnsServiceRegister(&req, nullptr);
  }
  else {
    status = DnsServiceDeRegister(&req, nullptr);
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

std::unique_ptr<::platf::deinit_t> start() {
  if(service(true)) {
    return nullptr;
  }

  BOOST_LOG(info) << "Registered Sunshine Gamestream service"sv;

  return std::make_unique<deinit_t>();
}
} // namespace platf::publish