/**
 * @file src/move_by_copy.h
 * @brief Declarations for the MoveByCopy utility class.
 */
#pragma once

// standard includes
#include <utility>

/**
 * @brief Contains utilities for moving objects by copying them.
 */
namespace move_by_copy_util {
  /**
   * When a copy is made, it moves the object
   * This allows you to move an object when a move can't be done.
   */
  template<class T>
  class MoveByCopy {
  public:
    /**
     * @brief Wrapped type moved through copy-shaped APIs.
     */
    typedef T move_type;

  private:
    move_type _to_move;

  public:
    /**
     * @brief Store a move-only value for transfer through copy-only call sites.
     *
     * @param to_move Object whose ownership is moved into this wrapper.
     */
    explicit MoveByCopy(move_type &&to_move):
        _to_move(std::move(to_move)) {
    }

    /**
     * @brief Move the stored object from another wrapper.
     *
     * @param other Wrapper whose stored object is moved into this object.
     */
    MoveByCopy(MoveByCopy &&other) = default;

    /**
     * @brief Copy by moving the stored object out of another wrapper.
     *
     * @param other Wrapper whose stored object will be moved despite the copy signature.
     */
    MoveByCopy(const MoveByCopy &other) {
      *this = other;
    }

    /**
     * @brief Move-assign the wrapped object from another wrapper.
     *
     * @param other Source object whose state is copied or moved into this object.
     * @return Reference to this wrapper.
     */
    MoveByCopy &operator=(MoveByCopy &&other) = default;

    /**
     * @brief Copy-assign by moving the wrapped object out of the source wrapper.
     *
     * @param other Source object whose state is copied or moved into this object.
     * @return Reference to this wrapper.
     */
    MoveByCopy &operator=(const MoveByCopy &other) {
      this->_to_move = std::move(const_cast<MoveByCopy &>(other)._to_move);

      return *this;
    }

    /**
     * @brief Move the wrapped object out of this helper.
     */
    operator move_type() {
      return std::move(_to_move);
    }
  };

  /**
   * @brief Copy a move-only value by moving from the source reference.
   *
   * @param movable Move-only object to transfer through a copy-shaped API.
   * @return Wrapper that moves the object when copied.
   */
  template<class T>
  MoveByCopy<T> cmove(T &movable) {
    return MoveByCopy<T>(std::move(movable));
  }

  // Do NOT use this unless you are absolutely certain the object to be moved is no longer used by the caller
  /**
   * @brief Copy-shape wrapper for moving from a const reference.
   *
   * @param movable Move-only object to transfer through a copy-shaped API.
   * @return Wrapper that moves the object when copied.
   */
  template<class T>
  MoveByCopy<T> const_cmove(const T &movable) {
    return MoveByCopy<T>(std::move(const_cast<T &>(movable)));
  }
}  // namespace move_by_copy_util
