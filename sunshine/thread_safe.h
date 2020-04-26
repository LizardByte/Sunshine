//
// Created by loki on 6/10/19.
//

#ifndef SUNSHINE_THREAD_SAFE_H
#define SUNSHINE_THREAD_SAFE_H

#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

#include "utility.h"

namespace safe {
template<class T>
class event_t {
  using status_t = util::optional_t<T>;

public:
  template<class...Args>
  void raise(Args &&...args) {
    std::lock_guard lg { _lock };
    if(!_continue) {
      return;
    }

    if constexpr (std::is_same_v<std::optional<T>, status_t>) {
      _status = std::make_optional<T>(std::forward<Args>(args)...);
    }
    else {
      _status = status_t { std::forward<Args>(args)... };
    }

    _cv.notify_all();
  }

  // pop and view shoud not be used interchangebly
  status_t pop() {
    std::unique_lock ul{ _lock };

    if (!_continue) {
      return util::false_v<status_t>;
    }

    while (!_status) {
      _cv.wait(ul);

      if (!_continue) {
        return util::false_v<status_t>;
      }
    }

    auto val = std::move(_status);
    _status = util::false_v<status_t>;
    return val;
  }

  // pop and view shoud not be used interchangebly
  template<class Rep, class Period>
  status_t pop(std::chrono::duration<Rep, Period> delay) {
    std::unique_lock ul{ _lock };

    if (!_continue) {
      return util::false_v<status_t>;
    }

    while (!_status) {
      if (!_continue || _cv.wait_for(ul, delay) == std::cv_status::timeout) {
        return util::false_v<status_t>;
      }
    }

    auto val = std::move(_status);
    _status = util::false_v<status_t>;
    return val;
  }

  // pop and view shoud not be used interchangebly
  const status_t &view() {
    std::unique_lock ul { _lock };

    if (!_continue) {
      return util::false_v<status_t>;
    }

    while (!_status) {
      _cv.wait(ul);

      if (!_continue) {
        return util::false_v<status_t>;
      }
    }

    return _status;
  }

  bool peek() {
    std::lock_guard lg { _lock };

    return _continue && (bool)_status;
  }

  void stop() {
    std::lock_guard lg{ _lock };

    _continue = false;

    _cv.notify_all();
  }

  void reset() {
    std::lock_guard lg{ _lock };

    _continue = true;

    _status = util::false_v<status_t>;
  }

  [[nodiscard]] bool running() const {
    return _continue;
  }
private:

  bool _continue { true };
  status_t _status { util::false_v<status_t> };

  std::condition_variable _cv;
  std::mutex _lock;
};

template<class T>
class queue_t {
  using status_t = util::optional_t<T>;

public:
  queue_t(std::uint32_t max_elements) : _max_elements { max_elements } {}

  template<class ...Args>
  void raise(Args &&... args) {
    std::lock_guard ul { _lock };

    if(!_continue) {
      return;
    }

    if(_queue.size() == _max_elements) {
      _queue.clear();
    }

    _queue.emplace_back(std::forward<Args>(args)...);

    _cv.notify_all();
  }

  bool peek() {
    std::lock_guard lg { _lock };

    return _continue && !_queue.empty();
  }

  template<class Rep, class Period>
  status_t pop(std::chrono::duration<Rep, Period> delay) {
    std::unique_lock ul { _lock };

    if (!_continue) {
      return util::false_v<status_t>;
    }

    while (_queue.empty()) {
      if (!_continue || _cv.wait_for(ul, delay) == std::cv_status::timeout) {
        return util::false_v<status_t>;
      }
    }

    auto val = std::move(_queue.front());
    _queue.erase(std::begin(_queue));

    return val;
  }

  status_t pop() {
    std::unique_lock ul { _lock };

    if (!_continue) {
      return util::false_v<status_t>;
    }

    while (_queue.empty()) {
      _cv.wait(ul);

      if (!_continue) {
        return util::false_v<status_t>;
      }
    }

    auto val = std::move(_queue.front());
    _queue.erase(std::begin(_queue));

    return val;
  }

  std::vector<T> &unsafe() {
    std::lock_guard { _lock };
    return _queue;
  }

  void stop() {
    std::lock_guard lg { _lock };

    _continue = false;

    _cv.notify_all();
  }

  [[nodiscard]] bool running() const {
    return _continue;
  }

private:

  bool _continue { true };
  std::uint32_t _max_elements;

  std::mutex _lock;
  std::condition_variable _cv;

  std::vector<T> _queue;
};

template<class T>
class shared_t {
public:
  using element_type = T;

  using construct_f = std::function<int(element_type &)>;
  using destruct_f = std::function<void(element_type &)>;

  struct ptr_t {
    shared_t *owner;

    ptr_t() : owner { nullptr } {}
    explicit ptr_t(shared_t *owner) : owner { owner } {}

    ptr_t(ptr_t &&ptr) noexcept : owner { ptr.owner } {
      ptr.owner = nullptr;
    }

    ptr_t(const ptr_t &ptr) noexcept : owner { ptr.owner } {
      if(!owner) {
        return;
      }

      auto tmp = ptr.owner->ref();
      tmp.owner = nullptr;
    }

    ptr_t &operator=(const ptr_t &ptr) noexcept {
      if(!ptr.owner) {
        release();

        return *this;
      }

      return *this = std::move(*ptr.owner->ref());
    }

    ptr_t &operator=(ptr_t &&ptr) noexcept {
      if(owner) {
        release();
      }

      std::swap(owner, ptr.owner);

      return *this;
    }

    ~ptr_t() {
      if(owner) {
        release();
      }
    }

    operator bool () const {
      return owner != nullptr;
    }

    void release() {
      std::lock_guard lg { owner->_lock };

      if(!--owner->_count) {
        owner->_destruct(*get());
        (*this)->~element_type();
      }

      owner = nullptr;
    }

    element_type *get() const {
      return reinterpret_cast<element_type*>(owner->_object_buf.data());
    }

    element_type *operator->() {
      return reinterpret_cast<element_type*>(owner->_object_buf.data());
    }
  };

  template<class FC, class FD>
  shared_t(FC && fc, FD &&fd) : _construct { std::forward<FC>(fc) }, _destruct { std::forward<FD>(fd) } {}
  [[nodiscard]] ptr_t ref() {
    std::lock_guard lg { _lock };

    if(!_count) {
      new(_object_buf.data()) element_type;
      if(_construct(*reinterpret_cast<element_type*>(_object_buf.data()))) {
        return ptr_t { nullptr };
      }
    }

    ++_count;

    return ptr_t { this };
  }
private:
  construct_f _construct;
  destruct_f _destruct;

  std::array<std::uint8_t, sizeof(element_type)> _object_buf;

  std::uint32_t _count;
  std::mutex _lock;
};

template<class T, class F_Construct, class F_Destruct>
auto make_shared(F_Construct &&fc, F_Destruct &&fd) {
  return shared_t<T> {
    std::forward<F_Construct>(fc), std::forward<F_Destruct>(fd)
  };
}

using signal_t = event_t<bool>;
}

#endif //SUNSHINE_THREAD_SAFE_H
