/**
 * @file src/platform/linux/publisher.h
 * @brief Instance-owned Avahi publisher factory for Linux service discovery.
 */
#pragma once

// standard includes
#include <cstdint>
#include <memory>
#include <string>

namespace avahi {
  class api_t;
}

namespace platf {
  class deinit_t;
}

namespace platf::publish {
  /**
   * @brief Start a publisher using an explicitly supplied Avahi API and service identity.
   * @param api Loaded Avahi functions retained for the publisher's lifetime.
   * @param instance_name Service name read during initialization and duplicated into Avahi-owned storage before return.
   * @param port HTTP service port to advertise.
   * @return Publisher owner, or nullptr when initialization fails.
   */
  [[nodiscard]] std::unique_ptr<::platf::deinit_t> start(std::shared_ptr<const avahi::api_t> api, const std::string &instance_name, std::uint16_t port);
}  // namespace platf::publish
