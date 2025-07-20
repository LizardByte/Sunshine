/**
 * @file src/platform/windows/wgc/wgc_ipc_session.h
 * @brief Shared IPC session for WGC capture that can be used by both RAM and VRAM implementations.
 */
#pragma once

#include <chrono>
#include <d3d11.h>
#include <memory>
#include <string>
#include <string_view>
#include <atomic>

#include "process_handler.h"
#include "shared_memory.h"
#include "src/video.h"
#include "src/utility.h"

// Include misc_utils outside of namespace to avoid nested namespace issues
#include "misc_utils.h"

namespace platf::dxgi {

  /**
   * Shared WGC IPC session that owns the helper process, pipe, shared texture,
   * keyed-mutex, frame event, metadata mapping... everything that both RAM & VRAM paths share.
   */
  class wgc_ipc_session_t {
  public:
    ~wgc_ipc_session_t();

    /**
     * Initialize the session with configuration and display name.
     * @param config Video configuration
     * @param display_name Display name
     * @param device D3D11 device for shared texture operations
     * @return 0 on success, non-zero on failure
     */
    int init(const ::video::config_t& config, std::string_view display_name, ID3D11Device* device);

    /**
     * Start the helper process and set up IPC connection on demand.
     */
    void lazy_init();

    /**
     * Clean up all resources.
     */
    void cleanup();

    /**
     * Blocking acquire of the next frame.
     * @param timeout Maximum time to wait for frame
     * @param gpu_tex_out Output parameter for the GPU texture pointer
     * @param meta_out Output parameter for frame metadata pointer
     * @return true on success, false on timeout/error
     */
    bool acquire(std::chrono::milliseconds timeout,
                 ID3D11Texture2D*& gpu_tex_out,
                 const FrameMetadata*& meta_out);

    /**
     * Release the keyed mutex and send heartbeat to helper process.
     */
    void release();

    /**
     * Check if the session should swap to DXGI due to secure desktop.
     * @return true if swap is needed
     */
    bool should_swap_to_dxgi() const { return _should_swap_to_dxgi; }

    // Accessors for texture properties
    UINT width() const { return _width; }
    UINT height() const { return _height; }
    bool is_initialized() const { return _initialized; }

  private:
    // IPC resources - using RAII wrappers for automatic cleanup
    std::unique_ptr<ProcessHandler> _process_helper;
    std::unique_ptr<AsyncNamedPipe> _pipe;
    safe_com_ptr<IDXGIKeyedMutex> _keyed_mutex;
    safe_com_ptr<ID3D11Texture2D> _shared_texture;

    // D3D11 device reference (not owned)
    ID3D11Device* _device = nullptr;

    // Frame state
    FrameMetadata _current_frame_metadata{};
    std::atomic<bool> _frame_ready{false};

    // State
    bool _initialized = false;
    std::atomic<bool> _should_swap_to_dxgi{false};
    UINT _width = 0;
    UINT _height = 0;
    uint32_t _timeout_count = 0;

    // Configuration
    ::video::config_t _config;
    std::string _display_name;

    // Helper methods
    bool setup_shared_texture(HANDLE shared_handle, UINT width, UINT height);
    void handle_shared_handle_message(const std::vector<uint8_t> &msg, bool &handle_received);
    void handle_frame_notification(const std::vector<uint8_t> &msg);
    void handle_secure_desktop_message(const std::vector<uint8_t> &msg);
    void initialize_mmcss_for_thread() const;
    void log_frame_diagnostics() const;
    std::chrono::milliseconds calculate_adjusted_timeout(std::chrono::milliseconds timeout) const;
    bool wait_for_frame(std::chrono::milliseconds adjusted_timeout);
    void handle_frame_timeout(std::chrono::milliseconds adjusted_timeout);
    void log_frame_sequence_diagnostics(const FrameMetadata *meta_out) const;
    void log_timing_diagnostics(uint64_t timestamp_before_wait, uint64_t timestamp_after_wait, uint64_t timestamp_after_mutex) const;
  };

} // namespace platf::dxgi
