/**
 * @file src/round_robin.h
 * @brief Declarations for a round-robin iterator.
 */
#pragma once

#include <iterator>

/**
 * @brief A round-robin iterator utility.
 * @tparam V The value type.
 * @tparam T The iterator type.
 */
namespace round_robin_util {
  template <class V, class T>
  class it_wrap_t {
  public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = V;
    using difference_type = V;
    using pointer = V *;
    using const_pointer = V const *;
    using reference = V &;
    using const_reference = V const &;

    typedef T iterator;
    typedef std::ptrdiff_t diff_t;

    iterator
    operator+=(diff_t step) {
      while (step-- > 0) {
        ++_this();
      }

      return _this();
    }

    iterator
    operator-=(diff_t step) {
      while (step-- > 0) {
        --_this();
      }

      return _this();
    }

    iterator
    operator+(diff_t step) {
      iterator new_ = _this();

      return new_ += step;
    }

    iterator
    operator-(diff_t step) {
      iterator new_ = _this();

      return new_ -= step;
    }

    diff_t
    operator-(iterator first) {
      diff_t step = 0;
      while (first != _this()) {
        ++step;
        ++first;
      }

      return step;
    }

    iterator
    operator++() {
      _this().inc();
      return _this();
    }
    iterator
    operator--() {
      _this().dec();
      return _this();
    }

    iterator
    operator++(int) {
      iterator new_ = _this();

      ++_this();

      return new_;
    }

    iterator
    operator--(int) {
      iterator new_ = _this();

      --_this();

      return new_;
    }

    reference
    operator*() { return *_this().get(); }
    const_reference
    operator*() const { return *_this().get(); }

    pointer
    operator->() { return &*_this(); }
    const_pointer
    operator->() const { return &*_this(); }

    bool
    operator!=(const iterator &other) const {
      return !(_this() == other);
    }

    bool
    operator<(const iterator &other) const {
      return !(_this() >= other);
    }

    bool
    operator>=(const iterator &other) const {
      return _this() == other || _this() > other;
    }

    bool
    operator<=(const iterator &other) const {
      return _this() == other || _this() < other;
    }

    bool
    operator==(const iterator &other) const { return _this().eq(other); };
    bool
    operator>(const iterator &other) const { return _this().gt(other); }

  private:
    iterator &
    _this() { return *static_cast<iterator *>(this); }
    const iterator &
    _this() const { return *static_cast<const iterator *>(this); }
  };

  template <class V, class It>
  class round_robin_t: public it_wrap_t<V, round_robin_t<V, It>> {
  public:
    using iterator = It;
    using pointer = V *;

    round_robin_t(iterator begin, iterator end):
        _begin(begin), _end(end), _pos(begin) {}

    void
    inc() {
      ++_pos;

      if (_pos == _end) {
        _pos = _begin;
      }
    }

    void
    dec() {
      if (_pos == _begin) {
        _pos = _end;
      }

      --_pos;
    }

    bool
    eq(const round_robin_t &other) const {
      return *_pos == *other._pos;
    }

    pointer
    get() const {
      return &*_pos;
    }

  private:
    It _begin;
    It _end;

    It _pos;
  };

  template <class V, class It>
  round_robin_t<V, It>
  make_round_robin(It begin, It end) {
    return round_robin_t<V, It>(begin, end);
  }
}  // namespace round_robin_util
