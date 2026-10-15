/**
 * @file src/string_hash.h
 * @brief Transparent hashing for containers with owning string keys.
 */
#pragma once

// standard includes
#include <cstddef>
#include <functional>
#include <string_view>

namespace util {
  /**
   * @brief Hash strings, string views, and C strings without temporary allocations.
   */
  struct transparent_string_hash {
    using is_transparent = void;  ///< Enable heterogeneous unordered-container lookup.

    /**
     * @brief Hash all characters in a string-like key, including embedded nulls in views.
     *
     * @param value Text to hash.
     * @return Hash shared by equivalent owning and non-owning string keys.
     */
    std::size_t operator()(std::string_view value) const noexcept {
      return std::hash<std::string_view> {}(value);
    }
  };
}  // namespace util
