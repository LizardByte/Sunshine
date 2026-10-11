/**
 * @file src/upnp.cpp
 * @brief Definitions for UPnP port mapping.
 */
// standard includes
#include <array>
#include <stddef.h>  // workaround for type_t error in miniupnpc 2.3.3, see https://github.com/miniupnp/miniupnp/commit/e263ab6f56c382e10fed31347ec68095d691a0e8

// lib includes
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

// local includes
#include "config.h"
#include "confighttp.h"
#include "globals.h"
#include "logging.h"
#include "network.h"
#include "nvhttp.h"
#include "rtsp.h"
#include "stream.h"
#include "upnp.h"
#include "utility.h"

using namespace std::literals;

namespace upnp {

  /**
   * @brief UPnP port mapping description and lease state.
   */
  struct mapping_t {
    struct {
      std::string wan;  ///< External gateway port exposed by the mapping.
      std::string lan;  ///< Internal Sunshine port targeted by the mapping.
      std::string proto;  ///< Transport protocol used by the mapping.
    } port;  ///< WAN/LAN/protocol tuple for the mapped port.

    std::string description;  ///< Human-readable UPnP lease description advertised to the gateway.
  };

  static std::string_view status_string(int status) {
    switch (status) {
      case 0:
        return "No IGD device found"sv;
      case 1:
        return "Valid IGD device found"sv;
      case 2:
        return "Valid IGD device found,  but it isn't connected"sv;
      case 3:
        return "A UPnP device has been found,  but it wasn't recognized as an IGD"sv;
    }

    return "Unknown status"sv;
  }

  /**
   * This function is a wrapper around UPNP_GetValidIGD() that returns the status code. There is a pre-processor
   * check to determine which version of the function to call based on the version of the MiniUPnPc library.
   */
  int UPNP_GetValidIGDStatus(device_t &device, urls_t *urls, IGDdatas *data, std::array<char, INET6_ADDRESS_STRLEN> &lan_addr) {
#if (MINIUPNPC_API_VERSION >= 18)
    return UPNP_GetValidIGD(device.get(), &urls->el, data, lan_addr.data(), (int) lan_addr.size(), nullptr, 0);
#else
    return UPNP_GetValidIGD(device.get(), &urls->el, data, lan_addr.data(), (int) lan_addr.size());
#endif
  }

  /**
   * @brief RAII helper that runs shutdown cleanup when destroyed.
   */
  class deinit_t: public platf::deinit_t {
  public:
    /**
     * @brief Start port mapping with the application's shutdown event.
     * @param settings Application network configuration.
     * @param host_mail Application-owned shutdown mailbox.
     */
    deinit_t(const config::settings_t &settings, safe::mail_t host_mail):
        settings {settings},
        host_mail {std::move(host_mail)} {
      auto rtsp = std::to_string(net::map_port(settings, rtsp_stream::RTSP_SETUP_PORT));
      auto video = std::to_string(net::map_port(settings, stream::VIDEO_STREAM_PORT));
      auto audio = std::to_string(net::map_port(settings, stream::AUDIO_STREAM_PORT));
      auto control = std::to_string(net::map_port(settings, stream::CONTROL_PORT));
      auto gs_http = std::to_string(net::map_port(settings, nvhttp::PORT_HTTP));
      auto gs_https = std::to_string(net::map_port(settings, nvhttp::PORT_HTTPS));
      auto wm_http = std::to_string(net::map_port(settings, confighttp::PORT_HTTPS));

      mappings.assign({
        {{rtsp, rtsp, "TCP"s}, "Sunshine - RTSP"s},
        {{video, video, "UDP"s}, "Sunshine - Video"s},
        {{audio, audio, "UDP"s}, "Sunshine - Audio"s},
        {{control, control, "UDP"s}, "Sunshine - Control"s},
        {{gs_http, gs_http, "TCP"s}, "Sunshine - Client HTTP"s},
        {{gs_https, gs_https, "TCP"s}, "Sunshine - Client HTTPS"s},
      });

      // Only map port for the Web Manager if it is configured to accept connection from WAN
      if (net::from_enum_string(settings.nvhttp.origin_web_ui_allowed) > net::LAN) {
        mappings.emplace_back(mapping_t {{wm_http, wm_http, "TCP"s}, "Sunshine - Web UI"s});
      }

      // Start the mapping thread
      upnp_thread = std::jthread {&deinit_t::upnp_thread_proc, this};
    }

    /**
     * @brief Destroy the UPnP deinitializer.
     */
    ~deinit_t() {
      upnp_thread.join();
    }

    /**
     * @brief Opens pinholes for IPv6 traffic if the IGD is capable.
     * @details Not many IGDs support this feature, so we perform error logging with debug level.
     * @return `true` if the pinholes were opened successfully.
     */
    bool create_ipv6_pinholes() {
      int err;
      device_t device {upnpDiscover(2000, nullptr, nullptr, 0, IPv6, 2, &err)};
      if (!device || err) {
        logging::debug("Couldn't discover any IPv6 UPNP devices"sv);
        return false;
      }

      IGDdatas data;
      urls_t urls;
      std::array<char, INET6_ADDRESS_STRLEN> lan_addr;
      auto status = upnp::UPNP_GetValidIGDStatus(device, &urls, &data, lan_addr);
      if (status != 1 && status != 2) {
        logging::debug([&](std::ostream &out) {
          out << "No valid IPv6 IGD: "sv << status_string(status);
        });
        return false;
      }

      if (data.IPv6FC.controlurl[0] != 0) {
        int firewallEnabled;
        int pinholeAllowed;

        // Check if this firewall supports IPv6 pinholes
        err = UPNP_GetFirewallStatus(urls->controlURL_6FC, data.IPv6FC.servicetype, &firewallEnabled, &pinholeAllowed);
        if (err == UPNPCOMMAND_SUCCESS) {
          logging::debug("UPnP IPv6 firewall control available. Firewall is {}, pinhole is {}", (firewallEnabled ? "enabled"sv : "disabled"sv), (pinholeAllowed ? "allowed"sv : "disallowed"sv));

          if (pinholeAllowed) {
            // Create pinholes for each port
            auto mapping_period = std::to_string(PORT_MAPPING_LIFETIME.count());
            auto shutdown_event = host_mail->event<bool>(mail::shutdown);

            for (auto it = std::begin(mappings); it != std::end(mappings) && !shutdown_event->peek(); ++it) {
              auto mapping = *it;
              std::array<char, 8> uniqueId;

              // Open a pinhole for the LAN port, since there will be no WAN->LAN port mapping on IPv6
              err = UPNP_AddPinhole(urls->controlURL_6FC, data.IPv6FC.servicetype, "", "0", lan_addr.data(), mapping.port.lan.c_str(), mapping.port.proto.c_str(), mapping_period.c_str(), uniqueId.data());
              if (err == UPNPCOMMAND_SUCCESS) {
                logging::debug("Successfully created pinhole for {} {}", mapping.port.proto, mapping.port.lan);
              } else {
                logging::debug("Failed to create pinhole for {} {}: {}", mapping.port.proto, mapping.port.lan, err);
              }
            }

            return err == 0;
          } else {
            logging::debug("IPv6 pinholes are not allowed by the IGD"sv);
            return false;
          }
        } else {
          logging::debug("Failed to get IPv6 firewall status: {}", err);
          return false;
        }
      } else {
        logging::debug("IPv6 Firewall Control is not supported by the IGD"sv);
        return false;
      }
    }

    /**
     * @brief Maps a port via UPnP.
     * @param data IGDdatas from UPNP_GetValidIGD()
     * @param urls urls_t from UPNP_GetValidIGD()
     * @param lan_addr Local IP address to map to
     * @param mapping Information about port to map
     * @return `true` on success.
     */
    bool map_upnp_port(const IGDdatas &data, const urls_t &urls, const std::string &lan_addr, const mapping_t &mapping) {
      std::array<char, 16> intClient;
      std::array<char, 6> intPort;
      std::array<char, 80> desc;
      std::array<char, 4> enabled;
      std::array<char, 16> leaseDuration;
      bool indefinite = false;

      // First check if this port is already mapped successfully
      logging::debug("Checking for existing UPnP port mapping for {}", mapping.port.wan);
      auto err = UPNP_GetSpecificPortMappingEntry(
        urls->controlURL,
        data.first.servicetype,
        // In params
        mapping.port.wan.c_str(),
        mapping.port.proto.c_str(),
        nullptr,
        // Out params
        intClient.data(),
        intPort.data(),
        desc.data(),
        enabled.data(),
        leaseDuration.data()
      );
      if (err == 714) {  // NoSuchEntryInArray
        logging::debug("Mapping entry not found for {}", mapping.port.wan);
      } else if (err == UPNPCOMMAND_SUCCESS) {
        // Some routers change the description, so we can't check that here
        if (std::string_view {intClient.data()} == lan_addr) {
          if (std::atoi(leaseDuration.data()) == 0) {
            logging::debug("Static mapping entry found for {}", mapping.port.wan);

            // It's a static mapping, so we're done here
            return true;
          } else {
            logging::debug("Mapping entry found for {} ({} seconds remaining)", mapping.port.wan, leaseDuration.data());
          }
        } else {
          logging::warning("UPnP conflict detected with: {}", intClient.data());

          // Some UPnP IGDs won't let unauthenticated clients delete other conflicting port mappings
          // for security reasons, but we will give it a try anyway.
          err = UPNP_DeletePortMapping(
            urls->controlURL,
            data.first.servicetype,
            mapping.port.wan.c_str(),
            mapping.port.proto.c_str(),
            nullptr
          );
          if (err) {
            logging::error("Unable to delete conflicting UPnP port mapping: {}", err);
            return false;
          }
        }
      } else {
        logging::error("UPNP_GetSpecificPortMappingEntry() failed: {}", err);

        // If we get a strange error from the router, we'll assume it's some old broken IGDv1
        // device and only use indefinite lease durations to hopefully avoid confusing it.
        if (err != 606) {  // Unauthorized
          indefinite = true;
        }
      }

      // Add/update the port mapping
      auto mapping_period = std::to_string(indefinite ? 0 : PORT_MAPPING_LIFETIME.count());
      err = UPNP_AddPortMapping(
        urls->controlURL,
        data.first.servicetype,
        mapping.port.wan.c_str(),
        mapping.port.lan.c_str(),
        lan_addr.data(),
        mapping.description.c_str(),
        mapping.port.proto.c_str(),
        nullptr,
        mapping_period.c_str()
      );

      if (err != UPNPCOMMAND_SUCCESS && !indefinite) {
        // This may be an old/broken IGD that doesn't like non-static mappings.
        logging::debug("Trying static mapping after failure: {}", err);
        err = UPNP_AddPortMapping(
          urls->controlURL,
          data.first.servicetype,
          mapping.port.wan.c_str(),
          mapping.port.lan.c_str(),
          lan_addr.data(),
          mapping.description.c_str(),
          mapping.port.proto.c_str(),
          nullptr,
          "0"
        );
      }

      if (err) {
        logging::error("Failed to map {} {}: {}", mapping.port.proto, mapping.port.lan, err);
        return false;
      }

      logging::debug("Successfully mapped {} {}", mapping.port.proto, mapping.port.lan);
      return true;
    }

    /**
     * @brief Unmaps all ports.
     * @param urls urls_t from UPNP_GetValidIGD()
     * @param data IGDdatas from UPNP_GetValidIGD()
     */
    void unmap_all_upnp_ports(const urls_t &urls, const IGDdatas &data) {
      for (auto it = std::begin(mappings); it != std::end(mappings); ++it) {
        auto status = UPNP_DeletePortMapping(
          urls->controlURL,
          data.first.servicetype,
          it->port.wan.c_str(),
          it->port.proto.c_str(),
          nullptr
        );

        if (status && status != 714) {  // NoSuchEntryInArray
          logging::warning("Failed to unmap {} {}: {}", it->port.proto, it->port.lan, status);
        } else {
          logging::debug("Successfully unmapped {} {}", it->port.proto, it->port.lan);
        }
      }
    }

    /**
     * @brief Maintains UPnP port forwarding rules
     */
    void upnp_thread_proc() {
      platf::set_thread_name("upnp");
      auto shutdown_event = host_mail->event<bool>(mail::shutdown);
      bool mapped = false;
      IGDdatas data;
      urls_t mapped_urls;
      auto address_family = net::get_effective_address_family(settings, net::af_from_enum_string(settings.sunshine.address_family));

      // Refresh UPnP rules every few minutes. They can be lost if the router reboots,
      // WAN IP address changes, or various other conditions.
      do {
        int err = 0;
        device_t device {upnpDiscover(2000, nullptr, nullptr, 0, IPv4, 2, &err)};
        if (!device || err) {
          logging::warning("Couldn't discover any IPv4 UPNP devices"sv);
          mapped = false;
          continue;
        }

        for (auto dev = device.get(); dev != nullptr; dev = dev->pNext) {
          logging::debug("Found device: {}", dev->descURL);
        }

        std::array<char, INET6_ADDRESS_STRLEN> lan_addr;

        urls_t urls;
        auto status = upnp::UPNP_GetValidIGDStatus(device, &urls, &data, lan_addr);
        if (status != 1 && status != 2) {
          logging::error([&](std::ostream &out) {
            out << status_string(status);
          });
          mapped = false;
          continue;
        }

        std::string lan_addr_str {lan_addr.data()};

        logging::debug("Found valid IGD device: {}", urls->rootdescURL);

        for (auto it = std::begin(mappings); it != std::end(mappings) && !shutdown_event->peek(); ++it) {
          map_upnp_port(data, urls, lan_addr_str, *it);
        }

        if (!mapped) {
          logging::info("Completed UPnP port mappings to {} via {}", lan_addr_str, urls->rootdescURL);
        }

        // If we are listening on IPv6 and the IGD has an IPv6 firewall enabled, try to create IPv6 firewall pinholes
        if (address_family == net::af_e::BOTH) {
          if (create_ipv6_pinholes() && !mapped) {
            // Only log the first time through
            logging::info("Successfully opened IPv6 pinholes on the IGD"sv);
          }
        }

        mapped = true;
        mapped_urls = std::move(urls);
      } while (!shutdown_event->view(REFRESH_INTERVAL));

      if (mapped) {
        // Unmap ports upon termination
        logging::info("Unmapping UPNP ports..."sv);
        unmap_all_upnp_ports(mapped_urls, data);
      }
    }

    std::vector<mapping_t> mappings;  ///< Port mappings Sunshine should keep registered with the gateway.
    const config::settings_t &settings;  ///< Startup port and address policy.
    safe::mail_t host_mail;  ///< Application-owned shutdown mailbox.
    std::jthread upnp_thread;  ///< Worker thread that refreshes mappings until shutdown.
  };

  /**
   * @brief Start UPnP port mapping and return its shutdown guard.
   */
  std::unique_ptr<platf::deinit_t> start(const config::settings_t &settings, safe::mail_t host_mail) {
    if (!settings.sunshine.flags[config::flag::UPNP]) {
      return nullptr;
    }

    return std::make_unique<deinit_t>(settings, std::move(host_mail));
  }
}  // namespace upnp
