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
   * @brief Get the valid IGD status.
   * @param device The device.
   * @param urls The URLs.
   * @param data The IGD data.
   * @param lan_addr The LAN address.
   * @return The UPnP Status.
   * @retval 0 No IGD found.
   * @retval 1 A valid connected IGD has been found.
   * @retval 2 A valid IGD has been found but it reported as not connected.
   * @retval 3 An UPnP device has been found but was not recognized as an IGD.
   */
  int UPNP_GetValidIGDStatus(device_t &device, urls_t *urls, IGDdatas *data, std::array<char, INET6_ADDRESS_STRLEN> &lan_addr);

  /**
   * @brief Start UPnP port mapping and return its shutdown guard.
   *
   * @return Start status.
   */
  [[nodiscard]] std::unique_ptr<platf::deinit_t> start();
}  // namespace upnp
