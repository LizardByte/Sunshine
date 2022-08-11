#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

#include "config.h"
#include "confighttp.h"
#include "main.h"
#include "network.h"
#include "nvhttp.h"
#include "rtsp.h"
#include "stream.h"
#include "upnp.h"
#include "utility.h"

using namespace std::literals;

namespace upnp {
constexpr auto INET6_ADDRESS_STRLEN = 46;

constexpr auto IPv4 = 0;
constexpr auto IPv6 = 1;

using device_t = util::safe_ptr<UPNPDev, freeUPNPDevlist>;

KITTY_USING_MOVE_T(urls_t, UPNPUrls, , {
  FreeUPNPUrls(&el);
});

struct mapping_t {
  struct {
    std::string wan;
    std::string lan;
  } port;

  std::string description;
  bool tcp;
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
  deinit_t(urls_t &&urls, IGDdatas data, std::vector<mapping_t> &&mapping)
      : urls { std::move(urls) }, data { data }, mapping { std::move(mapping) } {}

  ~deinit_t() {
    BOOST_LOG(info) << "Unmapping UPNP ports..."sv;
    unmap(urls, data, std::rbegin(mapping), std::rend(mapping));
  }

  urls_t urls;
  IGDdatas data;

  std::vector<mapping_t> mapping;
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

  std::array<char, INET6_ADDRESS_STRLEN> lan_addr;
  std::array<char, INET6_ADDRESS_STRLEN> wan_addr;

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

  if(!config::sunshine.flags[config::flag::UPNP]) {
    return nullptr;
  }

  auto rtsp     = std::to_string(map_port(stream::RTSP_SETUP_PORT));
  auto video    = std::to_string(map_port(stream::VIDEO_STREAM_PORT));
  auto audio    = std::to_string(map_port(stream::AUDIO_STREAM_PORT));
  auto control  = std::to_string(map_port(stream::CONTROL_PORT));
  auto gs_http  = std::to_string(map_port(nvhttp::PORT_HTTP));
  auto gs_https = std::to_string(map_port(nvhttp::PORT_HTTPS));
  auto wm_http  = std::to_string(map_port(confighttp::PORT_HTTPS));

  std::vector<mapping_t> mappings {
    { rtsp, rtsp, "RTSP setup port"s, true },
    { video, video, "Video stream port"s, false },
    { audio, audio, "Control stream port"s, false },
    { control, control, "Audio stream port"s, false },
    { gs_http, gs_http, "Gamestream http port"s, true },
    { gs_https, gs_https, "Gamestream https port"s, true },
  };

  // Only map port for the Web Manager if it is configured to accept connection from WAN
  if(net::from_enum_string(config::nvhttp.origin_web_ui_allowed) > net::LAN) {
    mappings.emplace_back(mapping_t { wm_http, wm_http, "Sunshine Web UI port"s, true });
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

  return std::make_unique<deinit_t>(std::move(urls), data, std::move(mappings));
}
} // namespace upnp