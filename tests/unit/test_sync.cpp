/**
 * @file tests/unit/test_sync.cpp
 * @brief Regression coverage for synchronized-value lock lifetimes.
 */

// standard includes
#include <array>
#include <stdexcept>
#include <thread>

// lib includes
#include <gtest/gtest.h>

// local includes
#include <src/sync.h>

namespace {
  /**
   * @brief BasicLockable mutex that detects an unreleased lock without requiring try_lock.
   */
  class basic_mutex {
  public:
    /**
     * @brief Acquire the mutex, rejecting recursive locking.
     */
    void lock() {
      if (held_) {
        throw std::logic_error {"mutex is already locked"};
      }
      held_ = true;
    }

    /**
     * @brief Release the mutex.
     */
    void unlock() noexcept {
      held_ = false;
    }

  private:
    bool held_ {false};  ///< Whether a guard currently owns this mutex.
  };
}  // namespace

TEST(SyncLockTest, SupportsBasicLockableAndReleasesOnException) {
  sync_util::sync_t<int, basic_mutex> value {0};
  EXPECT_THROW(
    [&value]() {
      const auto guard = value.lock();
      ++*value;
      throw std::runtime_error {"unwind the guard"};
    }(),
    std::runtime_error
  );

  EXPECT_NO_THROW([&value]() {
    const auto guard = value.lock();
    EXPECT_EQ(*value, 1);
  }());
  EXPECT_NO_THROW([&value]() {
    const auto guard = value.lock();
    EXPECT_EQ(*value, 1);
  }());
}

TEST(SyncLockTest, SerializesConcurrentUpdates) {
  sync_util::sync_t<int> value {0};
  {
    std::array<std::jthread, 4> workers;
    for (auto &worker : workers) {
      worker = std::jthread {[&value]() {
        for (int i = 0; i < 250; ++i) {
          const auto guard = value.lock();
          ++*value;
        }
      }};
    }
  }
  EXPECT_EQ(*value, 1000);
}
