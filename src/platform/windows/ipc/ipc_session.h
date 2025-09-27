/**
 * @file src/platform/windows/ipc/ipc_session.h
 * @brief Definitions for shared IPC session for WGC capture that can be used by both RAM and VRAM implementations.
 */
#pragma once

// standard includes
#include <array>
#include <atomic>
#include <chrono>
#include <d3d11.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>

// local includes
#include "misc_utils.h"
#include "pipes.h"
#include "process_handler.h"
#include "src/utility.h"
#include "src/video.h"

// platform includes
#include <winrt/base.h>

namespace platf::dxgi {

  /**
   * @brief Shared WGC IPC session encapsulating helper process, pipes, shared texture and sync primitives.
   * Manages lifecycle & communication with the helper process, duplication of shared textures, keyed mutex
   * coordination and frame availability signaling for both RAM & VRAM capture paths.
   */
  class ipc_session_t {
  public:
    /**
     * @brief Default destructor.
     */
    ~ipc_session_t() = default;

    /**
     * @brief Initialize the IPC session.
     * @param config Video configuration.
     * @param display_name Display name for the session.
     * @param device D3D11 device for shared texture operations (not owned).
     * @return `0` on success; non-zero otherwise.
     */
    int init(const ::video::config_t &config, std::string_view display_name, ID3D11Device *device);

    /**
     * @brief Start the helper process and set up IPC connection if not already initialized.
     * Performs a no-op if already initialized.
     */
    void initialize_if_needed();

    /**
     * @brief Acquire the next frame, blocking until available or timeout.
     * @param timeout Maximum time to wait for a frame.
     * @param gpu_tex_out Output ComPtr for the GPU texture (set on success).
     * @param frame_qpc_out Output for the frame QPC timestamp (`0` if unavailable).
     * @return Capture result enum indicating success, timeout, or failure.
     */
    capture_e acquire(std::chrono::milliseconds timeout, winrt::com_ptr<ID3D11Texture2D> &gpu_tex_out, uint64_t &frame_qpc_out);

    /**
     * @brief Release the keyed mutex.
     */
    void release();

    /**
     * @brief Check if the session should swap to DXGI due to secure desktop.
     * @return `true` if a swap to DXGI is needed, `false` otherwise.
     */
    bool should_swap_to_dxgi() const {
      return _should_swap_to_dxgi;
    }

    /**
     * @brief Check if the session should be reinitialized due to helper process issues.
     * @return `true` if reinitialization is needed, `false` otherwise.
     */
    bool should_reinit() const {
      return _force_reinit.load();
    }

    /**
     * @brief Get the width of the shared texture.
     * @return Width in pixels.
     */
    UINT width() const {
      return _width;
    }

    /**
     * @brief Get the height of the shared texture.
     * @return Height in pixels.
     */
    UINT height() const {
      return _height;
    }

    /**
     * @brief Check if the IPC session is initialized.
     * @return `true` if initialized, `false` otherwise.
     */
    bool is_initialized() const {
      return _initialized;
    }

  private:
    /**
     * @brief Set up shared texture from a shared handle by duplicating it.
     * @param shared_handle Shared handle from the helper process to duplicate.
     * @param width Width of the texture.
     * @param height Height of the texture.
     * @return `true` if setup was successful, `false` otherwise.
     */
    bool setup_shared_texture_from_shared_handle(HANDLE shared_handle, UINT width, UINT height);

    /**
     * @brief Handle a secure desktop notification from the helper process.
     * @param msg The message data received from the helper process.
     */
    void handle_secure_desktop_message(std::span<const uint8_t> msg);

    /**
     * @brief Wait for a new frame to become available or until timeout expires.
     * @param timeout Maximum duration to wait for a frame.
     * @return `true` if a frame became available; `false` if the timeout expired.
     */
    bool wait_for_frame(std::chrono::milliseconds timeout);

    /**
     * @brief Retrieve the adapter LUID for the current D3D11 device.
     * @param[out] luid_out Set to the adapter's LUID on success.
     * @return `true` if the adapter LUID was retrieved; `false` otherwise.
     */
    bool try_get_adapter_luid(LUID &luid_out);

    // --- members ---
    std::unique_ptr<ProcessHandler> _process_helper;  ///< Helper process owner.
    std::unique_ptr<AsyncNamedPipe> _pipe;  ///< Async control/message pipe.
    std::unique_ptr<INamedPipe> _frame_queue_pipe;  ///< Pipe providing frame ready notifications.
    winrt::com_ptr<IDXGIKeyedMutex> _keyed_mutex;  ///< Keyed mutex for shared texture.
    winrt::com_ptr<ID3D11Texture2D> _shared_texture;  ///< Shared texture duplicated from helper.
    winrt::com_ptr<ID3D11Device> _device;  ///< D3D11 device pointer (not owned).
    bool _frame_ready {false};  ///< Flag set when a new frame is ready.
    uint64_t _frame_qpc {0};  ///< QPC timestamp of latest frame.
    std::atomic<bool> _initializing {false};  ///< True while an initialization attempt is in progress.
    std::atomic<bool> _initialized {false};  ///< True once the most recent initialization attempt succeeded.
    std::atomic<bool> _should_swap_to_dxgi {false};  ///< True if capture should fallback.
    std::atomic<bool> _force_reinit {false};  ///< True if reinit required due to errors.
    UINT _width = 0;  ///< Shared texture width.
    UINT _height = 0;  ///< Shared texture height.
    uint32_t _timeout_count = 0;  ///< Consecutive acquire timeout counter.
    ::video::config_t _config;  ///< Cached video config.
    std::string _display_name;  ///< Display name copy.
  };

}  // namespace platf::dxgi
