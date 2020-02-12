//
// Created by loki on 16-4-19.
//

#ifndef SUNSHINE_SYNC_H
#define SUNSHINE_SYNC_H

#include <utility>
#include <mutex>
#include <array>

namespace util {

template<class T, std::size_t N = 1>
class sync_t {
public:
  static_assert(N > 0, "sync_t should have more than zero mutexes");
  using value_type = T;

  template<std::size_t I = 0>
  std::lock_guard<std::mutex> lock() {
    return std::lock_guard { std::get<I>(_lock) };
  }

  template<class ...Args>
  sync_t(Args&&... args) : raw {std::forward<Args>(args)... } {}

  sync_t &operator=(sync_t &&other) noexcept {
    for(auto &l : _lock) {
      l.lock();
    }

    for(auto &l : other._lock) {
      l.lock();
    }

    raw = std::move(other.raw);

    for(auto &l : _lock) {
      l.unlock();
    }

    for(auto &l : other._lock) {
      l.unlock();
    }

    return *this;
  }

  sync_t &operator=(sync_t &other) noexcept {
    for(auto &l : _lock) {
      l.lock();
    }

    for(auto &l : other._lock) {
      l.lock();
    }

    raw = other.raw;

    for(auto &l : _lock) {
      l.unlock();
    }

    for(auto &l : other._lock) {
      l.unlock();
    }

    return *this;
  }

  sync_t &operator=(const value_type &val) noexcept {
    for(auto &l : _lock) {
      l.lock();
    }

    raw = val;

    for(auto &l : _lock) {
      l.unlock();
    }

    return *this;
  }

  sync_t &operator=(value_type &&val) noexcept {
    for(auto &l : _lock) {
      l.lock();
    }

    raw = std::move(val);

    for(auto &l : _lock) {
      l.unlock();
    }

    return *this;
  }

  value_type *operator->() {
    return &raw;
  }

  value_type raw;
private:
  std::array<std::mutex, N> _lock;
};

}


#endif //T_MAN_SYNC_H
