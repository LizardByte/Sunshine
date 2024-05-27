/**
 * @file src/server/upnp.h
 * @brief Declarations for UPnP port mapping.
 */
#pragma once

#include "src/platform/common.h"

/**
 * @brief UPnP port mapping.
 */
namespace upnp {
  [[nodiscard]] std::unique_ptr<platf::deinit_t>
  start();
}
