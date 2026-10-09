/**
 * @file tests/unit/test_moonlight_common.cpp
 * @brief Verify that importing Moonlight does not change C++ errno constants.
 */
#include <array>
#include <cerrno>

namespace {
  constexpr std::array<int, 7> errno_before_moonlight {
    EAGAIN,
    EINTR,
    EWOULDBLOCK,
    EINPROGRESS,
    ETIMEDOUT,
    ECONNREFUSED,
    EMSGSIZE
  };  ///< CRT errno values before including Moonlight.
}

// Capture errno immediately after including Moonlight.
#include <src/moonlight_common.h>

namespace {
  constexpr std::array<int, 7> errno_after_moonlight {
    EAGAIN,
    EINTR,
    EWOULDBLOCK,
    EINPROGRESS,
    ETIMEDOUT,
    ECONNREFUSED,
    EMSGSIZE
  };  ///< Errno values immediately after including Moonlight.
}

// Boost must be parsed after the wrapper for this regression test.
#include <boost/system/error_code.hpp>
#include <gtest/gtest.h>

namespace moonlight_common_tests {
  /**
   * @brief Copy the first-include errno snapshot without sharing library types across translation units.
   * @param values Destination for seven errno values in the baseline array's order.
   * @return Whether all seven errno names were defined.
   */
  bool first_include_errno(int *values);
}  // namespace moonlight_common_tests

/**
 * @brief Including Moonlight preserves the CRT errno values.
 */
TEST(MoonlightCommonTests, PreservesCrtErrno) {
  EXPECT_EQ(errno_before_moonlight, errno_after_moonlight);
}

/**
 * @brief Boost's error constants match the original CRT errno values.
 */
TEST(MoonlightCommonTests, PreservesBoostErrno) {
  const std::array<int, 7> boost_errno {
    boost::system::errc::resource_unavailable_try_again,
    boost::system::errc::interrupted,
    boost::system::errc::operation_would_block,
    boost::system::errc::operation_in_progress,
    boost::system::errc::timed_out,
    boost::system::errc::connection_refused,
    boost::system::errc::message_size
  };
  EXPECT_EQ(errno_before_moonlight, boost_errno);
}

/**
 * @brief The wrapper initializes errno even when it is the first include.
 */
TEST(MoonlightCommonTests, FirstIncludePreservesCrtErrno) {
  std::array<int, 7> first_include_values {};
  EXPECT_TRUE(moonlight_common_tests::first_include_errno(first_include_values.data()));
  EXPECT_EQ(errno_before_moonlight, first_include_values);
}

/**
 * @brief Native connection-refused errors match Boost's generic condition.
 */
TEST(MoonlightCommonTests, ConnectionRefusedCondition) {
#ifdef _WIN32
  const boost::system::error_code ec {WSAECONNREFUSED, boost::system::system_category()};
#else
  const boost::system::error_code ec {ECONNREFUSED, boost::system::system_category()};
#endif
  EXPECT_EQ(ec, boost::system::errc::connection_refused);
}

/**
 * @brief Native timeouts match Boost's generic condition.
 */
TEST(MoonlightCommonTests, TimedOutCondition) {
#ifdef _WIN32
  const boost::system::error_code ec {WSAETIMEDOUT, boost::system::system_category()};
#else
  const boost::system::error_code ec {ETIMEDOUT, boost::system::system_category()};
#endif
  EXPECT_EQ(ec, boost::system::errc::timed_out);
}
