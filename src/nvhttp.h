/**
 * @file nvhttp.h
 */

// macros
#pragma once

// standard includes
#include <string>

// local includes
#include "thread_safe.h"

/**
 * @brief This namespace contains all the functions and variables related to the nvhttp (GameStream) server.
 */
namespace nvhttp {

  /**
   * @brief The protocol version.
   */
  constexpr auto VERSION = "7.1.431.-1";
  // The negative 4th version number tells Moonlight that this is Sunshine

  /**
   * @brief The GFE version we are replicating.
   */
  constexpr auto GFE_VERSION = "3.23.0.74";

  /**
   * @brief The HTTP port, as a difference from the config port.
   */
  constexpr auto PORT_HTTP = 0;

  /**
   * @brief The HTTPS port, as a difference from the config port.
   */
  constexpr auto PORT_HTTPS = -5;

  // functions
  void
  start();
  bool
  pin(std::string pin);
  void
  erase_all_clients();
}  // namespace nvhttp
