#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

#include "config.h"
#include "main.h"
#include "upnp.h"
#include "utility.h"

using namespace std::literals;

namespace upnp {
constexpr auto INET6_ADDRSTRLEN = 46;

constexpr auto IPv4 = 0;
constexpr auto IPv6 = 1;

using device_t = util::safe_ptr<UPNPDev, freeUPNPDevlist>;

KITTY_USING_MOVE_T(urls_t, UPNPUrls, , {
  FreeUPNPUrls(&el);
});

struct mapping_t {
  mapping_t(std::string &&wan, std::string &&lan, std::string &&description, bool tcp)
      : port { std::move(wan), std::move(lan) }, description { std::move(description) }, tcp { tcp } {}

  struct {
    std::string wan;
    std::string lan;
  } port;

  std::string description;
  bool tcp;
};

static const std::vector<mapping_t> mappings {
  { "48010"s, "48010"s, "RTSP setup port"s, false },
  { "47998"s, "47998"s, "Video stream port"s, false },
  { "47999"s, "47998"s, "Control stream port"s, false },
  { "48000"s, "48000"s, "Audio stream port"s, false },
  { "47989"s, "47989"s, "Gamestream http port"s, true },
  { "47984"s, "47984"s, "Gamestream https port"s, true },
  { "47990"s, "47990"s, "Sunshine Web Manager port"s, true },
};

void unmap(
  const urls_t &urls,
  const IGDdatas &data,
  std::vector<mapping_t>::const_reverse_iterator begin,
  std::vector<mapping_t>::const_reverse_iterator end) {

  BOOST_LOG(debug) << "Unmapping UPNP ports"sv;

  for(auto it = begin; it != end; ++it) {
    auto status = UPNP_DeletePortMapping(
      urls->controlURL,
      data.first.servicetype,
      it->port.wan.c_str(),
      it->tcp ? "TCP" : "UDP",
      nullptr);

    if(status) {
      BOOST_LOG(warning) << "Failed to unmap port ["sv << it->port.wan << "] to port ["sv << it->port.lan << "]: error code ["sv << status << ']';
      break;
    }
  }
}

class deinit_t : public platf::deinit_t {
public:
  using iter_t = std::vector<mapping_t>::const_reverse_iterator;
  deinit_t(urls_t &&urls, IGDdatas data, iter_t begin, iter_t end)
      : urls { std::move(urls) }, data { data }, begin { begin }, end { end } {}

  ~deinit_t() {
    unmap(urls, data, begin, end);
  }

  urls_t urls;
  IGDdatas data;

  iter_t begin;
  iter_t end;
};

static std::string_view status_string(int status) {
  switch(status) {
  case 0:
    return "No IGD device found"sv;
  case 1:
    return "Valid IGD device found"sv;
  case 2:
    return "A UPnP device has been found,  but it wasn't recognized as an IGD"sv;
  }

  return "Unknown status"sv;
}

std::unique_ptr<platf::deinit_t> start() {
  int err {};

  device_t device { upnpDiscover(2000, nullptr, nullptr, 0, IPv4, 2, &err) };
  if(!device || err) {
    BOOST_LOG(error) << "Couldn't discover any UPNP devices"sv;

    return nullptr;
  }

  for(auto dev = device.get(); dev != nullptr; dev = dev->pNext) {
    BOOST_LOG(debug) << "Found device: "sv << dev->descURL;
  }

  std::array<char, INET6_ADDRSTRLEN> lan_addr;
  std::array<char, INET6_ADDRSTRLEN> wan_addr;

  urls_t urls;
  IGDdatas data;

  auto status = UPNP_GetValidIGD(device.get(), &urls.el, &data, lan_addr.data(), lan_addr.size());
  if(status != 1) {
    BOOST_LOG(error) << status_string(status);
    return nullptr;
  }

  BOOST_LOG(debug) << "Found valid IGD device: "sv << urls->rootdescURL;

  if(UPNP_GetExternalIPAddress(urls->controlURL, data.first.servicetype, wan_addr.data())) {
    BOOST_LOG(warning) << "Could not get external ip"sv;
  }
  else {
    BOOST_LOG(debug) << "Found external ip: "sv << wan_addr.data();
    if(config::nvhttp.external_ip.empty()) {
      config::nvhttp.external_ip = wan_addr.data();
    }
  }

  auto it = std::begin(mappings);

  status = 0;
  for(; it != std::end(mappings); ++it) {
    status = UPNP_AddPortMapping(
      urls->controlURL,
      data.first.servicetype,
      it->port.wan.c_str(),
      it->port.lan.c_str(),
      lan_addr.data(),
      it->description.c_str(),
      it->tcp ? "TCP" : "UDP",
      nullptr,
      "86400");

    if(status) {
      BOOST_LOG(error) << "Failed to map port ["sv << it->port.wan << "] to port ["sv << it->port.lan << "]: error code ["sv << status << ']';
      break;
    }
  }

  if(status) {
    unmap(urls, data, std::make_reverse_iterator(it), std::rend(mappings));

    return nullptr;
  }

  return std::make_unique<deinit_t>(std::move(urls), data, std::rbegin(mappings), std::rend(mappings));
}
} // namespace upnp