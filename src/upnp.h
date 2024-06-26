/**
 * @file src/upnp.h
 * @brief Declarations for UPnP port mapping.
 */
#pragma once

#include "platform/common.h"

/**
 * @brief UPnP port mapping.
 */
namespace upnp {
  [[nodiscard]] std::unique_ptr<platf::deinit_t>
  start();
}
