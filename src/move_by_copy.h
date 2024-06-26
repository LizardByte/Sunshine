/**
 * @file src/move_by_copy.h
 * @brief Declarations for the MoveByCopy utility class.
 */
#pragma once

#include <utility>

/**
 * @brief Contains utilities for moving objects by copying them.
 */
namespace move_by_copy_util {
  /**
   * When a copy is made, it moves the object
   * This allows you to move an object when a move can't be done.
   */
  template <class T>
  class MoveByCopy {
  public:
    typedef T move_type;

  private:
    move_type _to_move;

  public:
    explicit MoveByCopy(move_type &&to_move):
        _to_move(std::move(to_move)) {}

    MoveByCopy(MoveByCopy &&other) = default;

    MoveByCopy(const MoveByCopy &other) {
      *this = other;
    }

    MoveByCopy &
    operator=(MoveByCopy &&other) = default;

    MoveByCopy &
    operator=(const MoveByCopy &other) {
      this->_to_move = std::move(const_cast<MoveByCopy &>(other)._to_move);

      return *this;
    }

    operator move_type() {
      return std::move(_to_move);
    }
  };

  template <class T>
  MoveByCopy<T>
  cmove(T &movable) {
    return MoveByCopy<T>(std::move(movable));
  }

  // Do NOT use this unless you are absolutely certain the object to be moved is no longer used by the caller
  template <class T>
  MoveByCopy<T>
  const_cmove(const T &movable) {
    return MoveByCopy<T>(std::move(const_cast<T &>(movable)));
  }
}  // namespace move_by_copy_util
