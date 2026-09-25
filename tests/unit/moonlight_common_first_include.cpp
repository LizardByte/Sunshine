/**
 * @file tests/unit/moonlight_common_first_include.cpp
 * @brief Capture errno when the Moonlight wrapper is the first include in its translation unit.
 */
#include <src/moonlight_common.h>

namespace {
  // Capture before any other include. Missing macros use zero only to keep a
  // broken wrapper testable at runtime; the separate completeness flag must pass.
  constexpr int errno_after_first_include[7] {
#ifdef EAGAIN
    EAGAIN,
#else
    0,
#endif
#ifdef EINTR
    EINTR,
#else
    0,
#endif
#ifdef EWOULDBLOCK
    EWOULDBLOCK,
#else
    0,
#endif
#ifdef EINPROGRESS
    EINPROGRESS,
#else
    0,
#endif
#ifdef ETIMEDOUT
    ETIMEDOUT,
#else
    0,
#endif
#ifdef ECONNREFUSED
    ECONNREFUSED,
#else
    0,
#endif
#ifdef EMSGSIZE
    EMSGSIZE
#else
    0
#endif
  };  ///< Errno values captured immediately after the first include.

#if defined(EAGAIN) && defined(EINTR) && defined(EWOULDBLOCK) && defined(EINPROGRESS) && defined(ETIMEDOUT) && defined(ECONNREFUSED) && defined(EMSGSIZE)
  constexpr bool errno_complete = true;  ///< Every required errno name was defined.
#else
  constexpr bool errno_complete = false;  ///< At least one required errno name was absent.
#endif
}  // namespace

namespace moonlight_common_tests {
  /**
   * @brief Copy the first-include errno snapshot without sharing library types across translation units.
   * @param values Destination for seven errno values.
   * @return Whether all seven errno names were defined.
   */
  bool first_include_errno(int *values) {
    for (int index = 0; index < 7; ++index) {
      values[index] = errno_after_first_include[index];
    }
    return errno_complete;
  }
}  // namespace moonlight_common_tests
