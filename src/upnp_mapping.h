/**
 * @file src/upnp_mapping.h
 * @brief Ownership and recovery records for UPnP port mappings.
 */
#pragma once

// standard includes
#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace upnp {
  /**
   * @brief A forwarding rule Sunshine owns, including its gateway and destination.
   */
  struct owned_mapping_t {
    std::string control_url;  ///< Gateway's SOAP endpoint.
    std::string service;  ///< Gateway's WAN connection service.
    std::string target;  ///< Host or inner router receiving the forwarded traffic.
    std::string external_port;  ///< Gateway port.
    std::string internal_port;  ///< Destination port.
    std::string protocol;  ///< TCP or UDP.
    std::string description;  ///< Description used when Sunshine created the rule.
    bool permanent = false;  ///< Rule requires recovery after an unexpected process exit.
    std::size_t generation = 0;  ///< Last successful discovery pass that wanted this exact rule.

    /**
     * @brief Compare persistent rule identities, excluding refresh bookkeeping.
     * @param other Rule to compare.
     * @return Whether both records identify the same forwarding rule.
     */
    bool operator==(const owned_mapping_t &other) const {
      return std::tie(control_url, service, target, external_port, internal_port, protocol, description) ==
             std::tie(other.control_url, other.service, other.target, other.external_port, other.internal_port, other.protocol, other.description);
    }
  };

  /**
   * @brief Relationship between a gateway's existing mapping and the requested rule.
   */
  enum class mapping_state_e {
    missing,  ///< Gateway explicitly reports no such mapping.
    ours,  ///< Destination, port and description match Sunshine's record.
    existing,  ///< Someone else's rule already forwards to the requested destination.
    conflict,  ///< Rule forwards elsewhere and must be left untouched.
    unknown,  ///< Lookup failed; ownership cannot be established.
  };

  /**
   * @brief Classify a lookup without treating a destination alone as proof of ownership.
   * @param result MiniUPnPc lookup result.
   * @param client Reported destination address.
   * @param port Reported destination port.
   * @param description Reported description.
   * @param wanted Requested or previously recorded rule.
   * @return Ownership classification.
   */
  inline mapping_state_e classify_mapping(int result, std::string_view client, std::string_view port, std::string_view description, const owned_mapping_t &wanted) {
    if (result == 714) {
      return mapping_state_e::missing;
    }
    if (result != 0) {
      return mapping_state_e::unknown;
    }
    if (client != wanted.target || port != wanted.internal_port) {
      return mapping_state_e::conflict;
    }
    return description == wanted.description ? mapping_state_e::ours : mapping_state_e::existing;
  }

  /**
   * @brief Encode one append-only recovery event before adding or after removing a permanent rule.
   * @param mapping Rule being recorded.
   * @param added Whether this records creation intent rather than completed removal.
   * @return A newline-delimited event, or an empty string for an invalid identity.
   */
  inline std::string mapping_journal_event(const owned_mapping_t &mapping, bool added) {
    for (const auto *field : {&mapping.control_url, &mapping.service, &mapping.target, &mapping.external_port, &mapping.internal_port, &mapping.protocol, &mapping.description}) {
      if (field->empty() || field->find_first_of("\r\n") != std::string::npos) {
        return {};
      }
    }
    std::ostringstream out;
    out << '\n'
        << (added ? '+' : '-') << ' ' << std::quoted(mapping.control_url) << ' ' << std::quoted(mapping.service) << ' '
        << std::quoted(mapping.target) << ' ' << std::quoted(mapping.external_port) << ' ' << std::quoted(mapping.internal_port)
        << ' ' << std::quoted(mapping.protocol) << ' ' << std::quoted(mapping.description) << '\n';
    return out.str();
  }

  /**
   * @brief Recover outstanding permanent rules, ignoring incomplete or invalid journal lines.
   * @param text Previously written recovery events.
   * @return Rules whose creation intent has no matching completed removal.
   */
  inline std::vector<owned_mapping_t> read_mapping_journal(const std::string &text) {
    std::vector<owned_mapping_t> mappings;
    std::istringstream input {text};
    std::string line;
    while (std::getline(input, line)) {
      std::istringstream entry {line};
      char event = 0;
      owned_mapping_t mapping;
      if (!(entry >> event >> std::quoted(mapping.control_url) >> std::quoted(mapping.service) >> std::quoted(mapping.target) >> std::quoted(mapping.external_port) >> std::quoted(mapping.internal_port) >> std::quoted(mapping.protocol) >> std::quoted(mapping.description)) || (event != '+' && event != '-') || mapping.control_url.empty() || mapping.service.empty() || mapping.target.empty() || mapping.external_port.empty() || mapping.internal_port.empty() || (mapping.protocol != "TCP" && mapping.protocol != "UDP") || mapping.description.empty()) {
        continue;
      }
      mapping.permanent = true;
      const auto found = std::find(mappings.begin(), mappings.end(), mapping);
      if (event == '+' && found == mappings.end()) {
        mappings.push_back(std::move(mapping));
      } else if (event == '-' && found != mappings.end()) {
        mappings.erase(found);
      }
    }
    return mappings;
  }
}  // namespace upnp
