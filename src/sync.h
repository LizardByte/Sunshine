/**
 * @file src/sync.h
 * @brief Declarations for synchronization utilities.
 */
#pragma once

// standard includes
#include <array>
#include <mutex>
#include <utility>

namespace sync_util {

  template<class T, class M = std::mutex>
  class sync_t {
  public:
    using value_t = T;
    using mutex_t = M;

    std::lock_guard<mutex_t> lock() {
      return std::lock_guard {_lock};
    }

    template<class... Args>
    sync_t(Args &&...args):
        raw {std::forward<Args>(args)...} {
    }

    sync_t &operator=(sync_t &&other) noexcept {
      std::lock(_lock, other._lock);

      raw = std::move(other.raw);

      _lock.unlock();
      other._lock.unlock();

      return *this;
    }

    sync_t &operator=(sync_t &other) noexcept {
      std::lock(_lock, other._lock);

      raw = other.raw;

      _lock.unlock();
      other._lock.unlock();

      return *this;
    }

    template<class V>
    sync_t &operator=(V &&val) {
      auto lg = lock();

      raw = val;

      return *this;
    }

    sync_t &operator=(const value_t &val) noexcept {
      auto lg = lock();

      raw = val;

      return *this;
    }

    sync_t &operator=(value_t &&val) noexcept {
      auto lg = lock();

      raw = std::move(val);

      return *this;
    }

    value_t *operator->() {
      return &raw;
    }

    value_t &operator*() {
      return raw;
    }

    const value_t &operator*() const {
      return raw;
    }

    value_t raw;

  private:
    mutex_t _lock;
  };

}  // namespace sync_util
