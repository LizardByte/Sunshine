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

namespace platf::dxgi {

  /**
   * @brief Shared WGC IPC session that owns the helper process, pipe, shared texture,
   * keyed-mutex, frame event, metadata mapping, and all resources required by both RAM & VRAM capture paths.
   * This class manages the lifecycle and communication with the WGC helper process, handles the creation and sharing
   * of textures between processes, and synchronizes frame acquisition using keyed mutexes and events.
   * It provides a unified interface for both RAM and VRAM capture implementations to interact with the shared IPC session.
   */
  class ipc_session_t {
  public:
    ~ipc_session_t() = default;

    /**
     * Initialize the session with configuration and display name.
     * @param config Video configuration
     * @param display_name Display name
     * @param device D3D11 device for shared texture operations
     * @return 0 on success, non-zero on failure
     */
    int init(const ::video::config_t &config, std::string_view display_name, ID3D11Device *device);
    /**
     * @brief Start the helper process and set up IPC connection on demand.
     * This method ensures that the helper process is running and the IPC connection is established.
     * If the session is not already initialized, it will launch the helper process and create the necessary
     * IPC resources for communication and shared texture access.
     * This is typically called before attempting to acquire frames or interact with the shared session.
     */
    void initialize_if_needed();

    /**
     * @brief Blocking acquire of the next frame.
     * @param timeout Maximum time to wait for frame
     * @param gpu_tex_out Output parameter for the GPU texture pointer
     * @param frame_qpc_out Output parameter for the frame QPC timestamp (0 if unavailable)
     */
    capture_e acquire(std::chrono::milliseconds timeout, ID3D11Texture2D *&gpu_tex_out, uint64_t &frame_qpc_out);

    /**
     * @brief Release the keyed mutex and send a heartbeat to the helper process.
     * This method releases the keyed mutex associated with the shared texture,
     * allowing the helper process to acquire it for the next frame. It also sends
     * a heartbeat message to the helper process to indicate that the session is still active.
     * This helps maintain synchronization between the capture session and the helper process.
     */
    void release();

    /**
     * @brief Check if the session should swap to DXGI due to secure desktop.
     * This method indicates whether the capture session should switch to using DXGI capture,
     * typically in response to entering a secure desktop environment (such as UAC prompts or login screens)
     * where the current capture method is no longer viable.
     * @return true if a swap to DXGI is needed, false otherwise.
     */
    bool should_swap_to_dxgi() const {
      return _should_swap_to_dxgi;
    }

    /**
     * @brief Check if the session should be reinitialized due to helper process issues.
     * This method returns whether the IPC session needs to be reinitialized, typically
     * due to errors or failures detected in the helper process or IPC communication.
     * When this returns true, the session should be cleaned up and re-initialized before further use.
     * @return true if reinitialization is needed, false otherwise.
     */
    bool should_reinit() const {
      return _force_reinit.load();
    }

    /**
     * @brief Get the width of the shared texture.
     * This value is set during initialization and used for texture operations.
     * @return Width of the shared texture in pixels.
     */
    UINT width() const {
      return _width;
    }

    /**
     * @brief Get the height of the shared texture.
     * This value is set during initialization and used for texture operations.
     * @return Height of the shared texture in pixels.
     */
    UINT height() const {
      return _height;
    }

    /**
     * @brief Check if the IPC session is initialized.
     * @return true if the session is initialized, false otherwise.
     */
    bool is_initialized() const {
      return _initialized;
    }

  private:
    std::unique_ptr<ProcessHandler> _process_helper;
    std::unique_ptr<AsyncNamedPipe> _pipe;
    std::unique_ptr<INamedPipe> _frame_queue_pipe;
    safe_com_ptr<IDXGIKeyedMutex> _keyed_mutex;
    safe_com_ptr<ID3D11Texture2D> _shared_texture;
    ID3D11Device *_device = nullptr;
    std::atomic<bool> _frame_ready {false};
    std::atomic<uint64_t> _frame_qpc {0};
    std::atomic<bool> _initialized {false};
    std::atomic<bool> _should_swap_to_dxgi {false};
    std::atomic<bool> _force_reinit {false};
    std::atomic<bool> _should_release {false};
    UINT _width = 0;
    UINT _height = 0;
    uint32_t _timeout_count = 0;
    ::video::config_t _config;
    std::string _display_name;

    /**
     * @brief Set up shared texture from a shared handle by duplicating it.
     *
     * @param shared_handle Shared handle from the helper process to duplicate
     * @param width Width of the texture  
     * @param height Height of the texture
     * @return true if setup was successful, false otherwise
     */
    bool setup_shared_texture_from_shared_handle(HANDLE shared_handle, UINT width, UINT height);

    /**
     * @brief Handle a secure desktop notification from the helper process.
     * Processes a message indicating that the system has entered a secure desktop environment
     * (such as UAC prompt or login screen), which may require the session to swap capture methods.
     * @param msg The message data received from the helper process
     */
    void handle_secure_desktop_message(std::span<const uint8_t> msg);

    /**
     * @brief Wait for a new frame to become available.
     * Blocks until a new frame is signaled by the helper process or the timeout expires.
     * Used to synchronize frame acquisition between processes.
     * @param timeout Maximum duration to wait for a frame
     * @return true if a frame became available, false if the timeout expired
     */
    bool wait_for_frame(std::chrono::milliseconds timeout);

    /**
     * @brief Attempt to retrieve the adapter LUID (Locally Unique Identifier) for the current D3D11 device.
     *
     * This function queries the underlying D3D11 device to obtain the adapter's LUID, which uniquely identifies
     * the graphics adapter in the system. The LUID is used for matching or verifying the correct GPU is being used
     * for shared texture operations and inter-process communication.
     *
     * @param[out] luid_out Reference to a LUID structure that will be set to the adapter's LUID on success.
     * @return true if the adapter LUID was successfully retrieved and luid_out is set; false otherwise.
     */
    bool try_get_adapter_luid(LUID &luid_out);
  };

}  // namespace platf::dxgi
