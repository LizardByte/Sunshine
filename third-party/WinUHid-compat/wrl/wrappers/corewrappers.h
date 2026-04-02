/**
 * @file wrl/wrappers/corewrappers.h
 * @brief MinGW-compatible shim for Microsoft WRL RAII handle wrappers.
 * @details WinUHid uses Wrappers::Event, Wrappers::FileHandle, and
 *          Wrappers::HandleT from WRL for RAII handle management.
 *          This header provides equivalent implementations for MinGW builds.
 */
#pragma once

#include <windows.h>

namespace Microsoft {
namespace WRL {
namespace Wrappers {

  namespace HandleTraits {

    struct HANDLETraits {
      static HANDLE GetInvalidValue() { return INVALID_HANDLE_VALUE; }
    };

    struct HANDLENullTraits {
      static HANDLE GetInvalidValue() { return nullptr; }
    };

  }  // namespace HandleTraits

  template <typename Traits>
  class HandleT {
  public:
    HandleT() noexcept : handle_(Traits::GetInvalidValue()) {}
    explicit HandleT(HANDLE h) noexcept : handle_(h) {}
    HandleT(HandleT &&other) noexcept : handle_(other.handle_) { other.handle_ = Traits::GetInvalidValue(); }
    HandleT &operator=(HandleT &&other) noexcept {
      if (this != &other) {
        Close();
        handle_ = other.handle_;
        other.handle_ = Traits::GetInvalidValue();
      }
      return *this;
    }
    HandleT(const HandleT &) = delete;
    HandleT &operator=(const HandleT &) = delete;
    ~HandleT() { Close(); }

    bool IsValid() const noexcept { return handle_ != Traits::GetInvalidValue(); }
    HANDLE Get() const noexcept { return handle_; }
    void Attach(HANDLE h) noexcept { Close(); handle_ = h; }
    HANDLE Detach() noexcept { HANDLE h = handle_; handle_ = Traits::GetInvalidValue(); return h; }
    void Close() noexcept {
      if (IsValid()) {
        CloseHandle(handle_);
        handle_ = Traits::GetInvalidValue();
      }
    }

  private:
    HANDLE handle_;
  };

  using Event = HandleT<HandleTraits::HANDLENullTraits>;
  using FileHandle = HandleT<HandleTraits::HANDLETraits>;

}  // namespace Wrappers
}  // namespace WRL
}  // namespace Microsoft
