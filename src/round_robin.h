/**
 * @file src/round_robin.h
 * @brief Declarations for a round-robin iterator.
 */
#pragma once

// standard includes
#include <iterator>

/**
 * @brief A round-robin iterator utility.
 * @tparam V The value type.
 * @tparam T The iterator type.
 */
namespace round_robin_util {
  /**
   * @brief CRTP base that provides iterator operators for round-robin iterators.
   */
  template<class V, class T>
  class it_wrap_t {
  public:
    /**
     * @brief Iterator category advertised to standard algorithms.
     */
    using iterator_category = std::random_access_iterator_tag;
    /**
     * @brief Value type exposed by the wrapped iterator.
     */
    using value_type = V;
    /**
     * @brief Difference type used for iterator movement.
     */
    using difference_type = V;
    /**
     * @brief Mutable pointer to a value in the cycled range.
     */
    using pointer = V *;
    /**
     * @brief Const pointer to a value in the cycled range.
     */
    using const_pointer = V const *;
    /**
     * @brief Mutable reference to a value in the cycled range.
     */
    using reference = V &;
    /**
     * @brief Const reference to a value in the cycled range.
     */
    using const_reference = V const &;

    /**
     * @brief Concrete iterator type supplied by the CRTP child.
     */
    typedef T iterator;
    /**
     * @brief Signed offset type used when moving through the range.
     */
    typedef std::ptrdiff_t diff_t;

    /**
     * @brief Advance this iterator by repeatedly wrapping at the end of the range.
     *
     * @param step Number of positions to advance.
     * @return Iterator positioned after the requested number of wrapped increments.
     */
    iterator operator+=(diff_t step) {
      while (step-- > 0) {
        ++_this();
      }

      return _this();
    }

    /**
     * @brief Move this iterator backward by repeatedly wrapping at the beginning of the range.
     *
     * @param step Number of positions to rewind.
     * @return Iterator positioned after the requested number of wrapped decrements.
     */
    iterator operator-=(diff_t step) {
      while (step-- > 0) {
        --_this();
      }

      return _this();
    }

    /**
     * @brief Return a copy advanced by a wrapped offset.
     *
     * @param step Number of positions to advance.
     * @return Advanced iterator copy.
     */
    iterator operator+(diff_t step) {
      iterator new_ = _this();

      return new_ += step;
    }

    /**
     * @brief Return a copy moved backward by a wrapped offset.
     *
     * @param step Number of positions to rewind.
     * @return Rewound iterator copy.
     */
    iterator operator-(diff_t step) {
      iterator new_ = _this();

      return new_ -= step;
    }

    /**
     * @brief Count wrapped increments needed to reach this iterator from another one.
     *
     * @param first Iterator used as the starting position.
     * @return Number of increments from `first` to this iterator.
     */
    diff_t operator-(iterator first) {
      diff_t step = 0;
      while (first != _this()) {
        ++step;
        ++first;
      }

      return step;
    }

    /**
     * @brief Advance to the next element, wrapping back to the beginning when needed.
     *
     * @return Iterator after advancing.
     */
    iterator operator++() {
      _this().inc();
      return _this();
    }

    /**
     * @brief Move to the previous element, wrapping to the end when needed.
     *
     * @return Iterator after moving backward.
     */
    iterator operator--() {
      _this().dec();
      return _this();
    }

    /**
     * @brief Advance to the next element and return the previous iterator position.
     *
     * @return Iterator position before advancing.
     */
    iterator operator++(int) {
      iterator new_ = _this();

      ++_this();

      return new_;
    }

    /**
     * @brief Move to the previous element and return the previous iterator position.
     *
     * @return Iterator position before moving backward.
     */
    iterator operator--(int) {
      iterator new_ = _this();

      --_this();

      return new_;
    }

    /**
     * @brief Dereference the current element in the cycled range.
     *
     * @return Mutable reference to the current value.
     */
    reference operator*() {
      return *_this().get();
    }

    /**
     * @brief Dereference the current element in the cycled range.
     *
     * @return Const reference to the current value.
     */
    const_reference operator*() const {
      return *_this().get();
    }

    /**
     * @brief Access the current element in the cycled range.
     *
     * @return Mutable pointer to the current value.
     */
    pointer operator->() {
      return &*_this();
    }

    /**
     * @brief Access the current element in the cycled range.
     *
     * @return Const pointer to the current value.
     */
    const_pointer operator->() const {
      return &*_this();
    }

    /**
     * @brief Compare whether two wrapped iterator positions differ.
     *
     * @param other Iterator position to compare against.
     * @return True when the iterators do not refer to the same position.
     */
    bool operator!=(const iterator &other) const {
      return !(_this() == other);
    }

    /**
     * @brief Compare whether this wrapped position sorts before another one.
     *
     * @param other Iterator position to compare against.
     * @return True when this iterator is ordered before `other`.
     */
    bool operator<(const iterator &other) const {
      return !(_this() >= other);
    }

    /**
     * @brief Compare whether this wrapped position sorts at or after another one.
     *
     * @param other Iterator position to compare against.
     * @return True when this iterator is ordered at or after `other`.
     */
    bool operator>=(const iterator &other) const {
      return _this() == other || _this() > other;
    }

    /**
     * @brief Compare whether this wrapped position sorts at or before another one.
     *
     * @param other Iterator position to compare against.
     * @return True when this iterator is ordered at or before `other`.
     */
    bool operator<=(const iterator &other) const {
      return _this() == other || _this() < other;
    }

    /**
     * @brief Compare whether two wrapped iterator positions are equal.
     *
     * @param other Iterator position to compare against.
     * @return True when the iterators refer to the same wrapped position.
     */
    bool operator==(const iterator &other) const {
      return _this().eq(other);
    };

    /**
     * @brief Compare whether this wrapped position sorts after another one.
     *
     * @param other Iterator position to compare against.
     * @return True when this iterator is ordered after `other`.
     */
    bool operator>(const iterator &other) const {
      return _this().gt(other);
    }

  private:
    iterator &_this() {
      return *static_cast<iterator *>(this);
    }

    const iterator &_this() const {
      return *static_cast<const iterator *>(this);
    }
  };

  /**
   * @brief Iterator that cycles indefinitely over a fixed begin/end range.
   */
  template<class V, class It>
  class round_robin_t: public it_wrap_t<V, round_robin_t<V, It>> {
  public:
    /**
     * @brief Underlying iterator type for the cycled range.
     */
    using iterator = It;
    /**
     * @brief Mutable pointer to values in the cycled range.
     */
    using pointer = V *;

    /**
     * @brief Construct a round-robin iterator over a fixed range.
     *
     * @param begin First element in the range to cycle through.
     * @param end One-past-the-end iterator for the range to cycle through.
     */
    round_robin_t(iterator begin, iterator end):
        _begin(begin),
        _end(end),
        _pos(begin) {
    }

    /**
     * @brief Advance the iterator to the next element.
     */
    void inc() {
      ++_pos;

      if (_pos == _end) {
        _pos = _begin;
      }
    }

    /**
     * @brief Move the iterator to the previous element.
     */
    void dec() {
      if (_pos == _begin) {
        _pos = _end;
      }

      --_pos;
    }

    /**
     * @brief Compare two iterators for equality.
     *
     * @param other Iterator or container to compare against.
     * @return True when both iterators point to equivalent values.
     */
    bool eq(const round_robin_t &other) const {
      return *_pos == *other._pos;
    }

    /**
     * @brief Return the currently wrapped value or handle.
     *
     * @return Underlying native handle or object pointer.
     */
    pointer get() const {
      return &*_pos;
    }

  private:
    It _begin;
    It _end;

    It _pos;
  };

  /**
   * @brief Create a round-robin iterator over a fixed range.
   *
   * @param begin Iterator or pointer marking the start of the input range.
   * @param end Iterator or pointer marking the end of the input range.
   * @return Iterator initialized to `begin` and wrapping before `end`.
   */
  template<class V, class It>
  round_robin_t<V, It> make_round_robin(It begin, It end) {
    return round_robin_t<V, It>(begin, end);
  }
}  // namespace round_robin_util
