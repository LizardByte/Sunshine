/**
 * @file src/upnp.h
 * @brief Declarations for UPnP port mapping.
 */
#pragma once

// lib includes
#include <miniupnpc/miniupnpc.h>

// local includes
#include "platform/common.h"

/**
 * @brief UPnP port mapping.
 */
namespace upnp {
  constexpr auto INET6_ADDRESS_STRLEN = 46;  ///< Protocol or platform constant for inet6 address strlen.
  constexpr auto IPv4 = 0;  ///< I pv4.
  constexpr auto IPv6 = 1;  ///< I pv6.
  constexpr auto PORT_MAPPING_LIFETIME = 3600s;  ///< GameStream port offset for port mapping lifetime.
  constexpr auto REFRESH_INTERVAL = 120s;  ///< Protocol or platform constant for refresh interval.

  /**
   * @brief Owning pointer to miniupnpc device discovery results.
   */
  using device_t = util::safe_ptr<UPNPDev, freeUPNPDevlist>;

  KITTY_USING_MOVE_T(urls_t, UPNPUrls, , {
    FreeUPNPUrls(&el);
  });  ///< Alias for element type.

  /**
   * @brief Result codes returned by the installed MiniUPnPc library.
   * @details Project-owned equivalent of the UPNP_GetValidIGD() result codes. MiniUPnPc 2.2.8
   * (API version 18) introduced the private-address result, renumbering the values that follow it;
   * the library's own UPNP_* symbolic names only exist starting with MiniUPnPc 2.3.0 (API version 19),
   * so this enum centralizes the version-dependent numbering instead of relying on those names.
   */
  enum class igd_status_e : int {
    none = 0,  ///< No IGD was found.
    connected = 1,  ///< A connected IGD was found.
#if (MINIUPNPC_API_VERSION >= 18)
    private_ip = 2,  ///< A connected IGD with a reserved address was found.
#endif
    disconnected,  ///< A disconnected IGD was found.
    unknown_device,  ///< The device was not recognized as an IGD.
  };

  /**
   * @brief Check whether a UPNP_GetValidIGD() status code identifies a usable IGD.
   * @details Accepts every status code that identifies a valid IGD (connected, private-IP, or
   * disconnected). The private_ip member only exists with MiniUPnPc >= 2.2.8 (API version 18);
   * disconnected is 2 before API 18 and 3 starting with API 18, and unknown_device is 3 or 4
   * respectively. Callers don't need to handle the version difference themselves.
   * @param status The status returned by UPNP_GetValidIGDStatus(), converted to igd_status_e.
   * @return `true` if the status identifies a usable IGD, `false` otherwise.
   */
  [[nodiscard]] bool is_valid_igd(igd_status_e status);

  /**
   * @brief Get the valid IGD status.
   * @param device The device.
   * @param urls The URLs.
   * @param data The IGD data.
   * @param lan_addr The LAN address.
   * @return The IGD status as an igd_status_e value, converted from the raw UPNP_GetValidIGD() result.
   * @retval igd_status_e::none No IGD found.
   * @retval igd_status_e::connected A valid connected IGD has been found.
   * @retval igd_status_e::private_ip A valid connected IGD with a private IP address (MiniUPnPc >= 2.2.8 only).
   * @retval igd_status_e::disconnected A valid IGD has been found but it reported as not connected.
   * @retval igd_status_e::unknown_device An UPnP device has been found but was not recognized as an IGD.
   */
  igd_status_e UPNP_GetValidIGDStatus(device_t &device, urls_t *urls, IGDdatas *data, std::array<char, INET6_ADDRESS_STRLEN> &lan_addr);

  /**
   * @brief Start UPnP port mapping and return its shutdown guard.
   *
   * @return Start status.
   */
  [[nodiscard]] std::unique_ptr<platf::deinit_t> start();
}  // namespace upnp
