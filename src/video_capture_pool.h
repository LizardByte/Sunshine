/**
 * @file src/video_capture_pool.h
 * @brief Image reuse and idle allocation trimming for shared display capture.
 */
#pragma once

#include "platform/common.h"

#include <chrono>
#include <list>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace video::detail {
  /**
   * @brief Own capture images in reuse order while tracking recent concurrent demand.
   */
  class capture_image_pool_t {
  public:
    /**
     * @brief Reserve image slots without allocating image buffers.
     * @param capacity Maximum simultaneous capture image allocations.
     */
    explicit capture_image_pool_t(std::size_t capacity = 12):
        images_(capacity) {
    }

    /**
     * @brief Reuse an idle image or allocate the first empty slot.
     * @tparam Factory Callable that allocates one image for the active display.
     * @tparam Clock Callable supplying the trimming timestamp after allocation.
     * @param allocate Allocation callback invoked only when no idle image exists.
     * @param clock Clock invoked only after an image is acquired.
     * @return Image with its frame timestamp cleared, or nullptr when full or allocation fails.
     */
    template<typename Factory, typename Clock>
    std::shared_ptr<platf::img_t> acquire(const Factory &allocate, const Clock &clock) {
      auto image = reuse_idle();
      if (!image) {
        image = allocate_empty(allocate);
      }
      if (image) {
        trim(clock());
        image->frame_timestamp.reset();
      }
      return image;
    }

    /**
     * @brief Release pool references before waiting for display teardown.
     * @details Recently observed demand is retained across display reinitialization.
     */
    void clear() {
      for (auto &image : images_) {
        image.reset();
      }
    }

  private:
    /** @brief Iterator locating an image slot in reuse order. */
    using iterator_t = std::list<std::shared_ptr<platf::img_t>>::iterator;

    /**
     * @brief Move a selected slot to the front without changing pool capacity.
     * @param slot Selected image slot.
     * @return Retained image, possibly nullptr after a failed allocation.
     */
    std::shared_ptr<platf::img_t> promote(iterator_t slot) {
      auto image = *slot;
      if (slot != images_.begin()) {
        images_.erase(slot);
        images_.push_front(image);
      }
      return image;
    }

    /**
     * @brief Find the first allocated image without an external owner.
     * @return Retained image, or nullptr when every allocated image is busy.
     */
    std::shared_ptr<platf::img_t> reuse_idle() {
      for (auto slot = images_.begin(); slot != images_.end(); ++slot) {
        if (*slot && slot->use_count() == 1) {
          return promote(slot);
        }
      }
      return {};
    }

    /**
     * @brief Allocate the first empty slot, preserving one attempt per acquisition.
     * @tparam Factory Callable that allocates an image.
     * @param allocate Image allocation callback.
     * @return Retained image, or nullptr when no empty slot exists or allocation fails.
     */
    template<typename Factory>
    std::shared_ptr<platf::img_t> allocate_empty(const Factory &allocate) {
      for (auto slot = images_.begin(); slot != images_.end(); ++slot) {
        if (!*slot) {
          *slot = allocate();
          return promote(slot);
        }
      }
      return {};
    }

    /**
     * @brief Count allocated slots and images retained outside the pool.
     * @return Allocated count followed by externally used count.
     */
    std::pair<std::size_t, std::size_t> usage() const {
      std::size_t allocated = 0;
      std::size_t used = 0;
      for (const auto &image : images_) {
        if (image) {
          ++allocated;
          if (image.use_count() > 1) {
            ++used;
          }
        }
      }
      return {allocated, used};
    }

    /**
     * @brief Retain the largest concurrent demand observed within three seconds.
     * @param used Current externally retained image count.
     * @param now Timestamp sampled after acquisition.
     * @return Number of allocated images worth retaining.
     */
    std::size_t trim_target(std::size_t used, std::chrono::steady_clock::time_point now) {
      if (used_timestamps_.size() <= used) {
        used_timestamps_.resize(used + 1);
      }
      used_timestamps_[used] = now;
      std::size_t target = used;
      for (std::size_t count = used; count < used_timestamps_.size(); ++count) {
        if (used_timestamps_[count] && now - *used_timestamps_[count] < std::chrono::seconds {3}) {
          target = count;
        }
      }
      return target;
    }

    /**
     * @brief Release least recently reused idle images until demand is satisfied.
     * @param count Number of idle allocations to release.
     */
    void release_idle(std::size_t count) {
      for (auto slot = images_.rbegin(); slot != images_.rend(); ++slot) {
        if (*slot && slot->use_count() == 1) {
          slot->reset();
          if (--count == 0) {
            break;
          }
        }
      }
    }

    /**
     * @brief Trim idle allocations after the acquired image has an external owner.
     * @param now Timestamp sampled after acquisition.
     */
    void trim(std::chrono::steady_clock::time_point now) {
      const auto [allocated, used] = usage();
      const auto target = trim_target(used, now);
      if (allocated > target) {
        release_idle(allocated - target);
        used_timestamps_.resize(target + 1);
      }
    }

    std::list<std::shared_ptr<platf::img_t>> images_;  ///< Slots ordered from most recently selected to least recently selected.
    std::vector<std::optional<std::chrono::steady_clock::time_point>> used_timestamps_;  ///< Last observation of each simultaneous external image count.
  };
}  // namespace video::detail
