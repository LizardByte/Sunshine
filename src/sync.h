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

  /**
   * @brief Value wrapper that pairs an object with the mutex protecting it.
   */
  template<class T, class M = std::mutex>
  class sync_t {
  public:
    /**
     * @brief Type stored behind the synchronization wrapper.
     */
    using value_t = T;
    /**
     * @brief Mutex type used to protect the stored value.
     */
    using mutex_t = M;

    /**
     * @brief Acquire the underlying lock or keyed mutex.
     *
     * @return Lock guard owning the synchronized object until destruction.
     */
    std::lock_guard<mutex_t> lock() {
      return std::lock_guard {_lock};
    }

    /**
     * @brief Initialize the protected object from forwarded arguments.
     *
     * @param args Arguments forwarded to the protected object's constructor.
     */
    template<class... Args>
    sync_t(Args &&...args):
        raw {std::forward<Args>(args)...} {
    }

    /**
     * @brief Move another synchronized value into this instance while locking both wrappers.
     *
     * @param other Source object whose state is copied or moved into this object.
     * @return Reference to this wrapper.
     */
    sync_t &operator=(sync_t &&other) noexcept {
      std::lock(_lock, other._lock);

      raw = std::move(other.raw);

      _lock.unlock();
      other._lock.unlock();

      return *this;
    }

    /**
     * @brief Copy another synchronized value into this instance while locking both wrappers.
     *
     * @param other Source object whose state is copied or moved into this object.
     * @return Reference to this wrapper.
     */
    sync_t &operator=(sync_t &other) noexcept {
      std::lock(_lock, other._lock);

      raw = other.raw;

      _lock.unlock();
      other._lock.unlock();

      return *this;
    }

    /**
     * @brief Assign a new value while holding this wrapper's lock.
     *
     * @param val Value assigned to the synchronized object.
     * @return Reference to this wrapper.
     */
    template<class V>
    sync_t &operator=(V &&val) {
      auto lg = lock();

      raw = val;

      return *this;
    }

    /**
     * @brief Copy-assign the protected value while holding this wrapper's lock.
     *
     * @param val Value assigned to the synchronized object.
     * @return Reference to this wrapper.
     */
    sync_t &operator=(const value_t &val) noexcept {
      auto lg = lock();

      raw = val;

      return *this;
    }

    /**
     * @brief Move-assign the protected value while holding this wrapper's lock.
     *
     * @param val Value assigned to the synchronized object.
     * @return Reference to this wrapper.
     */
    sync_t &operator=(value_t &&val) noexcept {
      auto lg = lock();

      raw = std::move(val);

      return *this;
    }

    /**
     * @brief Access the protected value directly.
     *
     * @return Pointer to the protected value.
     */
    value_t *operator->() {
      return &raw;
    }

    /**
     * @brief Dereference the protected value.
     *
     * @return Mutable reference to the protected value.
     */
    value_t &operator*() {
      return raw;
    }

    /**
     * @brief Dereference the protected value.
     *
     * @return Const reference to the protected value.
     */
    const value_t &operator*() const {
      return raw;
    }

    value_t raw;  ///< Protected value accessed while this helper's lock is held.

  private:
    mutex_t _lock;
  };

}  // namespace sync_util
