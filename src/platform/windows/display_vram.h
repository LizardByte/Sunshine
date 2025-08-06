/**
 * @file src/platform/windows/display_vram.h
 * @brief DXGI/D3D11 VRAM image structures and utilities for Windows platform display capture.
 */
#pragma once

// standard includes
#include <memory>

// local includes
#include "display.h"

// platform includes
#include <d3d11.h>
#include <dxgi.h>

namespace platf::dxgi {

  struct img_d3d_t: public platf::img_t {
    // These objects are owned by the display_t's ID3D11Device
    texture2d_t capture_texture;
    render_target_t capture_rt;
    keyed_mutex_t capture_mutex;

    // This is the shared handle used by hwdevice_t to open capture_texture
    HANDLE encoder_texture_handle = {};

    // Set to true if the image corresponds to a dummy texture used prior to
    // the first successful capture of a desktop frame
    bool dummy = false;

    // Set to true if the image is blank (contains no content at all, including a cursor)
    bool blank = true;

    // Unique identifier for this image
    uint32_t id = 0;

    // DXGI format of this image texture
    DXGI_FORMAT format;

    virtual ~img_d3d_t() override {
      if (encoder_texture_handle) {
        CloseHandle(encoder_texture_handle);
      }
    };
  };

}  // namespace platf::dxgi
