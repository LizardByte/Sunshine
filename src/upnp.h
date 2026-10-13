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
   * @brief Check whether a UPNP_GetValidIGD() status code identifies a usable IGD.
   * @details MiniUPnPc 2.2.8 (API version 18) renumbered the UPNP_GetValidIGD() result codes and added
   * UPNP_PRIVATEIP_IGD. This function accepts every status code that identifies a valid IGD
   * (connected, private-IP, or disconnected) under both the old and new numbering, so callers
   * don't need to handle the version difference themselves.
   * @param status The status code returned by UPNP_GetValidIGD().
   * @return `true` if the status identifies a usable IGD, `false` otherwise.
   */
  [[nodiscard]] bool is_valid_igd(int status);

  /**
   * @brief Get the valid IGD status.
   * @param device The device.
   * @param urls The URLs.
   * @param data The IGD data.
   * @param lan_addr The LAN address.
   * @return The UPnP Status.
   * @retval 0 No IGD found.
   * @retval 1 A valid connected IGD has been found.
   * @retval 2 A valid IGD has been found but it reported as not connected (MiniUPnPc < 2.2.8), or a valid
   * connected IGD with a private IP address (MiniUPnPc >= 2.2.8).
   * @retval 3 An UPnP device has been found but was not recognized as an IGD (MiniUPnPc < 2.2.8), or a
   * valid IGD has been found but it reported as not connected (MiniUPnPc >= 2.2.8).
   * @retval 4 An UPnP device has been found but was not recognized as an IGD (MiniUPnPc >= 2.2.8).
   */
  int UPNP_GetValidIGDStatus(device_t &device, urls_t *urls, IGDdatas *data, std::array<char, INET6_ADDRESS_STRLEN> &lan_addr);

  /**
   * @brief Start UPnP port mapping and return its shutdown guard.
   *
   * @return Start status.
   */
  [[nodiscard]] std::unique_ptr<platf::deinit_t> start();
}  // namespace upnp
