/**
 * @file src/platform/windows/display_wgc.cpp
 * @brief Windows Game Capture (WGC) IPC display implementation with shared session helper and DXGI fallback.
 */

// standard includes
#include <algorithm>
#include <chrono>
#include <dxgi1_2.h>
#include <wrl/client.h>

// local includes
#include "ipc/ipc_session.h"
#include "ipc/misc_utils.h"
#include "src/logging.h"
#include "src/platform/windows/display.h"
#include "src/platform/windows/display_vram.h"
#include "src/platform/windows/misc.h"
#include "src/utility.h"

// platform includes
#include <winrt/base.h>

namespace platf::dxgi {

  display_wgc_ipc_vram_t::display_wgc_ipc_vram_t() = default;

  display_wgc_ipc_vram_t::~display_wgc_ipc_vram_t() = default;

  int display_wgc_ipc_vram_t::init(const ::video::config_t &config, const std::string &display_name) {
    _config = config;
    _display_name = display_name;

    if (display_base_t::init(config, display_name)) {
      return -1;
    }

    capture_format = DXGI_FORMAT_UNKNOWN;  // Start with unknown format (prevents race condition/crash on first frame)

    // Create session
    _ipc_session = std::make_unique<ipc_session_t>();
    if (_ipc_session->init(config, display_name, device.get())) {
      return -1;
    }

    return 0;
  }

  capture_e display_wgc_ipc_vram_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    if (!_ipc_session) {
      return capture_e::error;
    }

    // We return capture::reinit for most scenarios because the logic in picking which mode to capture is all handled in the factory function.
    if (_ipc_session->should_swap_to_dxgi()) {
      return capture_e::reinit;
    }

    // Generally this only becomes true if the helper process has crashed or is otherwise not responding.
    if (_ipc_session->should_reinit()) {
      return capture_e::reinit;
    }

    _ipc_session->initialize_if_needed();
    if (!_ipc_session->is_initialized()) {
      return capture_e::error;
    }

    // Most of the code below is copy and pasted from display_vram_t with some elements removed such as cursor blending.

    texture2d_t src;
    uint64_t frame_qpc;

    auto capture_status = acquire_next_frame(timeout, src, frame_qpc, cursor_visible);

    if (capture_status == capture_e::ok) {
      // Got a new frame - process it normally
      auto frame_timestamp = std::chrono::steady_clock::now() - qpc_time_difference(qpc_counter(), frame_qpc);
      D3D11_TEXTURE2D_DESC desc;
      src->GetDesc(&desc);

      // If we don't know the capture format yet, grab it from this texture
      if (capture_format == DXGI_FORMAT_UNKNOWN) {
        capture_format = desc.Format;
        BOOST_LOG(info) << "Capture format [" << dxgi_format_to_string(capture_format) << ']';
      }

      // It's possible for our display enumeration to race with mode changes and result in
      // mismatched image pool and desktop texture sizes. If this happens, just reinit again.
      if (desc.Width != width_before_rotation || desc.Height != height_before_rotation) {
        BOOST_LOG(info) << "Capture size changed ["sv << width_before_rotation << 'x' << height_before_rotation << " -> "sv << desc.Width << 'x' << desc.Height << ']';
        return capture_e::reinit;
      }

      // It's also possible for the capture format to change on the fly. If that happens,
      // reinitialize capture to try format detection again and create new images.
      if (capture_format != desc.Format) {
        BOOST_LOG(info) << "Capture format changed ["sv << dxgi_format_to_string(capture_format) << " -> "sv << dxgi_format_to_string(desc.Format) << ']';
        return capture_e::reinit;
      }

      // Get a free image from the pool
      std::shared_ptr<platf::img_t> img;
      if (!pull_free_image_cb(img)) {
        return capture_e::interrupted;
      }

      auto d3d_img = std::static_pointer_cast<img_d3d_t>(img);
      d3d_img->blank = false;  // image is always ready for capture

      // Assign the shared texture from the session to the img_d3d_t
      d3d_img->capture_texture.reset(src.get()); // no need to AddRef() because acquire on ipc uses copy_to

      // Get the keyed mutex from the shared texture
      HRESULT status = d3d_img->capture_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **) &d3d_img->capture_mutex);
      if (FAILED(status)) {
        BOOST_LOG(error) << "Failed to query IDXGIKeyedMutex from shared texture [0x"sv << util::hex(status).to_string_view() << ']';
        return capture_e::error;
      }

      // Get the shared handle for the encoder
      resource1_t resource;
      status = d3d_img->capture_texture->QueryInterface(__uuidof(IDXGIResource1), (void **) &resource);
      if (FAILED(status)) {
        BOOST_LOG(error) << "Failed to query IDXGIResource1 [0x"sv << util::hex(status).to_string_view() << ']';
        return capture_e::error;
      }

      // Create NT shared handle for the encoder device to use
      status = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &d3d_img->encoder_texture_handle);
      if (FAILED(status)) {
        BOOST_LOG(error) << "Failed to create NT shared texture handle [0x"sv << util::hex(status).to_string_view() << ']';
        return capture_e::error;
      }

      // Set the format and other properties
      d3d_img->format = capture_format;
      d3d_img->pixel_pitch = get_pixel_pitch();
      d3d_img->row_pitch = d3d_img->pixel_pitch * d3d_img->width;
      d3d_img->data = (std::uint8_t *) d3d_img->capture_texture.get();

      img->frame_timestamp = frame_timestamp;
      img_out = img;

      // Cache this frame for potential reuse
      last_cached_frame = img;

      return capture_e::ok;

    } else if (capture_status == capture_e::timeout && config::video.capture == "wgcc" && last_cached_frame) {
      // No new frame available, but we have a cached frame - forward it
      // This mimics the DDUP ofa::forward_last_img behavior
      // Only do this for genuine timeouts, not for errors
      img_out = last_cached_frame;
      // Update timestamp to current time to maintain proper timing
      if (img_out) {
        img_out->frame_timestamp = std::chrono::steady_clock::now();
      }

      return capture_e::ok;

    } else {
      // For the default mode just return the capture status on timeouts.
      return capture_status;
    }
  }

  capture_e display_wgc_ipc_vram_t::acquire_next_frame(std::chrono::milliseconds timeout, texture2d_t &src, uint64_t &frame_qpc, bool cursor_visible) {
    if (!_ipc_session) {
      return capture_e::error;
    }

    winrt::com_ptr<ID3D11Texture2D> gpu_tex;
    auto status = _ipc_session->acquire(timeout, gpu_tex, frame_qpc);

    if (status != capture_e::ok) {
      return status;
    }

    gpu_tex.copy_to(&src);

    return capture_e::ok;
  }

  capture_e display_wgc_ipc_vram_t::release_snapshot() {
    if (_ipc_session) {
      _ipc_session->release();
    }
    return capture_e::ok;
  }

  int display_wgc_ipc_vram_t::dummy_img(platf::img_t *img_base) {
    _ipc_session->initialize_if_needed();

    // In certain scenarios (e.g., Windows login screen or when no user session is active),
    // the IPC session may fail to initialize, making it impossible to perform encoder tests via WGC.
    // In such cases, we must check if the session was initialized; if not, fall back to DXGI for dummy image generation.
    if (_ipc_session->is_initialized()) {
      return display_vram_t::complete_img(img_base, true);
    }

    auto temp_dxgi = std::make_unique<display_ddup_vram_t>();
    if (temp_dxgi->init(_config, _display_name) == 0) {
      return temp_dxgi->dummy_img(img_base);
    } else {
      BOOST_LOG(error) << "Failed to initialize DXGI fallback for dummy_img";
      return -1;
    }
  }

  std::shared_ptr<display_t> display_wgc_ipc_vram_t::create(const ::video::config_t &config, const std::string &display_name) {
    // Check if secure desktop is currently active

    if (platf::dxgi::is_secure_desktop_active()) {
      // Secure desktop is active, use DXGI fallback
      BOOST_LOG(debug) << "Secure desktop detected, using DXGI fallback for WGC capture (VRAM)";
      auto disp = std::make_shared<temp_dxgi_vram_t>();
      if (!disp->init(config, display_name)) {
        return disp;
      }
    } else {
      // Secure desktop not active, use WGC IPC
      BOOST_LOG(debug) << "Using WGC IPC implementation (VRAM)";
      auto disp = std::make_shared<display_wgc_ipc_vram_t>();
      if (!disp->init(config, display_name)) {
        return disp;
      }
    }

    return nullptr;
  }

  display_wgc_ipc_ram_t::display_wgc_ipc_ram_t() = default;

  display_wgc_ipc_ram_t::~display_wgc_ipc_ram_t() = default;

  int display_wgc_ipc_ram_t::init(const ::video::config_t &config, const std::string &display_name) {
    // Save config for later use
    _config = config;
    _display_name = display_name;

    // Initialize the base display class
    if (display_base_t::init(config, display_name)) {
      return -1;
    }

    // Override dimensions with config values (base class sets them to monitor native resolution)
    width = config.width;
    height = config.height;
    width_before_rotation = config.width;
    height_before_rotation = config.height;

    // Create session
    _ipc_session = std::make_unique<ipc_session_t>();
    if (_ipc_session->init(config, display_name, device.get())) {
      return -1;
    }

    return 0;
  }

  capture_e display_wgc_ipc_ram_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    if (!_ipc_session) {
      return capture_e::error;
    }

    if (_ipc_session->should_swap_to_dxgi()) {
      return capture_e::reinit;
    }

    // If the helper process crashed or was terminated forcefully by the user, we will re-initialize it.
    if (_ipc_session->should_reinit()) {
      return capture_e::reinit;
    }

    _ipc_session->initialize_if_needed();
    if (!_ipc_session->is_initialized()) {
      return capture_e::error;
    }

    winrt::com_ptr<ID3D11Texture2D> gpu_tex;
    uint64_t frame_qpc = 0;
    auto status = _ipc_session->acquire(timeout, gpu_tex, frame_qpc);

    if (status != capture_e::ok) {
      // For constant FPS mode (wgcc), try to return cached frame on timeout
      if (status == capture_e::timeout && config::video.capture == "wgcc" && last_cached_frame) {
        // No new frame available, but we have a cached frame - forward it
        // This mimics the DDUP ofa::forward_last_img behavior
        // Only do this for genuine timeouts, not for errors
        img_out = last_cached_frame;
        // Update timestamp to current time to maintain proper timing
        if (img_out) {
          img_out->frame_timestamp = std::chrono::steady_clock::now();
        }

        return capture_e::ok;
      }

      // For the default mode just return the capture status on timeouts.
      return status;
    }

    // Get description of the captured texture
    D3D11_TEXTURE2D_DESC desc;
    gpu_tex->GetDesc(&desc);

    // If we don't know the capture format yet, grab it from this texture
    if (capture_format == DXGI_FORMAT_UNKNOWN) {
      capture_format = desc.Format;
      BOOST_LOG(info) << "Capture format [" << dxgi_format_to_string(capture_format) << ']';
    }

    // Check for size changes
    if (desc.Width != width || desc.Height != height) {
      BOOST_LOG(info) << "Capture size changed [" << width << 'x' << height << " -> " << desc.Width << 'x' << desc.Height << ']';
      _ipc_session->release();
      return capture_e::reinit;
    }

    // Check for format changes
    if (capture_format != desc.Format) {
      BOOST_LOG(info) << "Capture format changed [" << dxgi_format_to_string(capture_format) << " -> " << dxgi_format_to_string(desc.Format) << ']';
      _ipc_session->release();
      return capture_e::reinit;
    }

    // Create or recreate staging texture if needed
    // Use OUR dimensions (width/height), not the source texture dimensions
    if (!texture ||
        width != _last_width ||
        height != _last_height ||
        capture_format != _last_format) {
      D3D11_TEXTURE2D_DESC t {};
      t.Width = width;  // Use display dimensions, not session dimensions
      t.Height = height;  // Use display dimensions, not session dimensions
      t.Format = capture_format;
      t.ArraySize = 1;
      t.MipLevels = 1;
      t.SampleDesc = {1, 0};
      t.Usage = D3D11_USAGE_STAGING;
      t.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

      auto hr = device->CreateTexture2D(&t, nullptr, &texture);
      if (FAILED(hr)) {
        BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Failed to create staging texture: " << hr;
        _ipc_session->release();
        return capture_e::error;
      }

      _last_width = width;
      _last_height = height;
      _last_format = capture_format;

      BOOST_LOG(info) << "[display_wgc_ipc_ram_t] Created staging texture: "
                      << width << "x" << height << ", format: " << capture_format;
    }

    // Copy from GPU to CPU
    device_ctx->CopyResource(texture.get(), gpu_tex.get());

    // Get a free image from the pool
    if (!pull_free_image_cb(img_out)) {
      _ipc_session->release();
      return capture_e::interrupted;
    }

    auto img = img_out.get();

    // If we don't know the final capture format yet, encode a dummy image
    if (capture_format == DXGI_FORMAT_UNKNOWN) {
      BOOST_LOG(debug) << "Capture format is still unknown. Encoding a blank image";

      if (dummy_img(img)) {
        _ipc_session->release();
        return capture_e::error;
      }
    } else {
      // Map the staging texture for CPU access (making it inaccessible for the GPU)
      auto hr = device_ctx->Map(texture.get(), 0, D3D11_MAP_READ, 0, &img_info);
      if (FAILED(hr)) {
        BOOST_LOG(error) << "[display_wgc_ipc_ram_t] Failed to map staging texture: " << hr;
        _ipc_session->release();
        return capture_e::error;
      }

      // Now that we know the capture format, we can finish creating the image
      if (complete_img(img, false)) {
        device_ctx->Unmap(texture.get(), 0);
        img_info.pData = nullptr;
        _ipc_session->release();
        return capture_e::error;
      }

      // Copy exactly like display_ram.cpp: height * img_info.RowPitch
      std::copy_n((std::uint8_t *) img_info.pData, height * img_info.RowPitch, img->data);

      // Unmap the staging texture to allow GPU access again
      device_ctx->Unmap(texture.get(), 0);
      img_info.pData = nullptr;
    }

    // Set frame timestamp

    auto frame_timestamp = std::chrono::steady_clock::now() - qpc_time_difference(qpc_counter(), frame_qpc);
    img->frame_timestamp = frame_timestamp;

    // Cache this frame for potential reuse in constant FPS mode
    last_cached_frame = img_out;

    _ipc_session->release();
    return capture_e::ok;
  }

  capture_e display_wgc_ipc_ram_t::release_snapshot() {
    // Not used in RAM path since we handle everything in snapshot()
    return capture_e::ok;
  }

  int display_wgc_ipc_ram_t::dummy_img(platf::img_t *img_base) {
    _ipc_session->initialize_if_needed();

    // In certain scenarios (e.g., Windows login screen or when no user session is active),
    // the IPC session may fail to initialize, making it impossible to perform encoder tests via WGC.
    // In such cases, we must check if the session was initialized; if not, fall back to DXGI for dummy image generation.
    if (_ipc_session->is_initialized()) {
      return display_ram_t::complete_img(img_base, true);
    }

    auto temp_dxgi = std::make_unique<display_ddup_ram_t>();
    if (temp_dxgi->init(_config, _display_name) == 0) {
      return temp_dxgi->dummy_img(img_base);
    } else {
      BOOST_LOG(error) << "Failed to initialize DXGI fallback for dummy image check there may be no displays available.";
      return -1;
    }
  }

  std::shared_ptr<display_t> display_wgc_ipc_ram_t::create(const ::video::config_t &config, const std::string &display_name) {
    // Check if secure desktop is currently active

    if (platf::dxgi::is_secure_desktop_active()) {
      // Secure desktop is active, use DXGI fallback
      BOOST_LOG(debug) << "Secure desktop detected, using DXGI fallback for WGC capture (RAM)";
      auto disp = std::make_shared<temp_dxgi_ram_t>();
      if (!disp->init(config, display_name)) {
        return disp;
      }
    } else {
      // Secure desktop not active, use WGC IPC
      BOOST_LOG(debug) << "Using WGC IPC implementation (RAM)";
      auto disp = std::make_shared<display_wgc_ipc_ram_t>();
      if (!disp->init(config, display_name)) {
        return disp;
      }
    }

    return nullptr;
  }

  capture_e temp_dxgi_vram_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    // Check periodically if secure desktop is still active
    if (auto now = std::chrono::steady_clock::now(); now - _last_check_time >= CHECK_INTERVAL) {
      _last_check_time = now;
      if (!platf::dxgi::is_secure_desktop_active()) {
        BOOST_LOG(debug) << "DXGI Capture is no longer necessary, swapping back to WGC!";
        return capture_e::reinit;
      }
    }

    // Call parent DXGI duplication implementation
    return display_ddup_vram_t::snapshot(pull_free_image_cb, img_out, timeout, cursor_visible);
  }

  capture_e temp_dxgi_ram_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    // Check periodically if secure desktop is still active
    if (auto now = std::chrono::steady_clock::now(); now - _last_check_time >= CHECK_INTERVAL) {
      _last_check_time = now;
      if (!platf::dxgi::is_secure_desktop_active()) {
        BOOST_LOG(debug) << "DXGI Capture is no longer necessary, swapping back to WGC!";
        return capture_e::reinit;
      }
    }

    // Call parent DXGI duplication implementation
    return display_ddup_ram_t::snapshot(pull_free_image_cb, img_out, timeout, cursor_visible);
  }

}  // namespace platf::dxgi
