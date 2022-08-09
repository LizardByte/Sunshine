#include <filesystem>
#include <iomanip>
#include <sstream>


// prevent clang format from "optimizing" the header include order
// clang-format off
#include <winsock2.h>
#include <iphlpapi.h>
#include <windows.h>
#include <winuser.h>
#include <ws2tcpip.h>
// clang-format on

#include "src/main.h"
#include "src/utility.h"

using namespace std::literals;
namespace platf {
using adapteraddrs_t = util::c_ptr<IP_ADAPTER_ADDRESSES>;

std::filesystem::path appdata() {
  return L"."sv;
}

std::string from_sockaddr(const sockaddr *const socket_address) {
  char data[INET6_ADDRSTRLEN];

  auto family = socket_address->sa_family;
  if(family == AF_INET6) {
    inet_ntop(AF_INET6, &((sockaddr_in6 *)socket_address)->sin6_addr, data, INET6_ADDRSTRLEN);
  }

  if(family == AF_INET) {
    inet_ntop(AF_INET, &((sockaddr_in *)socket_address)->sin_addr, data, INET_ADDRSTRLEN);
  }

  return std::string { data };
}

std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const ip_addr) {
  char data[INET6_ADDRSTRLEN];

  auto family = ip_addr->sa_family;
  std::uint16_t port;
  if(family == AF_INET6) {
    inet_ntop(AF_INET6, &((sockaddr_in6 *)ip_addr)->sin6_addr, data, INET6_ADDRSTRLEN);
    port = ((sockaddr_in6 *)ip_addr)->sin6_port;
  }

  if(family == AF_INET) {
    inet_ntop(AF_INET, &((sockaddr_in *)ip_addr)->sin_addr, data, INET_ADDRSTRLEN);
    port = ((sockaddr_in *)ip_addr)->sin_port;
  }

  return { port, std::string { data } };
}

adapteraddrs_t get_adapteraddrs() {
  adapteraddrs_t info { nullptr };
  ULONG size = 0;

  while(GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, info.get(), &size) == ERROR_BUFFER_OVERFLOW) {
    info.reset((PIP_ADAPTER_ADDRESSES)malloc(size));
  }

  return info;
}

std::string get_mac_address(const std::string_view &address) {
  adapteraddrs_t info = get_adapteraddrs();
  for(auto adapter_pos = info.get(); adapter_pos != nullptr; adapter_pos = adapter_pos->Next) {
    for(auto addr_pos = adapter_pos->FirstUnicastAddress; addr_pos != nullptr; addr_pos = addr_pos->Next) {
      if(adapter_pos->PhysicalAddressLength != 0 && address == from_sockaddr(addr_pos->Address.lpSockaddr)) {
        std::stringstream mac_addr;
        mac_addr << std::hex;
        for(int i = 0; i < adapter_pos->PhysicalAddressLength; i++) {
          if(i > 0) {
            mac_addr << ':';
          }
          mac_addr << std::setw(2) << std::setfill('0') << (int)adapter_pos->PhysicalAddress[i];
        }
        return mac_addr.str();
      }
    }
  }
  BOOST_LOG(warning) << "Unable to find MAC address for "sv << address;
  return "00:00:00:00:00:00"s;
}

HDESK syncThreadDesktop() {
  auto hDesk = OpenInputDesktop(DF_ALLOWOTHERACCOUNTHOOK, FALSE, GENERIC_ALL);
  if(!hDesk) {
    auto err = GetLastError();
    BOOST_LOG(error) << "Failed to Open Input Desktop [0x"sv << util::hex(err).to_string_view() << ']';

    return nullptr;
  }

  if(!SetThreadDesktop(hDesk)) {
    auto err = GetLastError();
    BOOST_LOG(error) << "Failed to sync desktop to thread [0x"sv << util::hex(err).to_string_view() << ']';
  }

  CloseDesktop(hDesk);

  return hDesk;
}

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
} // namespace platf