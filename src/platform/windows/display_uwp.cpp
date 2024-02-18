/**
 * @file src/platform/windows/display_uwp.cpp
 * @brief WinRT Windows.Graphics.Capture API
 */
#include <dxgi1_2.h>

#include "display.h"

#include "misc.h"
#include "src/logging.h"

#include <windows.graphics.capture.interop.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.graphics.directx.direct3d11.h>

namespace platf {
  using namespace std::literals;
}

namespace winrt {
  using namespace Windows::Foundation;
  using namespace Windows::Graphics::Capture;
  using namespace Windows::Graphics::DirectX::Direct3D11;

  extern "C" {
  HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(::IDXGIDevice *dxgiDevice, ::IInspectable **graphicsDevice);
  }
  struct
#if WINRT_IMPL_HAS_DECLSPEC_UUID
    __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
#endif
      IDirect3DDxgiInterfaceAccess: ::IUnknown {
    virtual HRESULT __stdcall GetInterface(REFIID id, void **object) = 0;
  };
}  // namespace winrt
#if !WINRT_IMPL_HAS_DECLSPEC_UUID
static constexpr GUID GUID__IDirect3DDxgiInterfaceAccess = {
  0xA9B3D012, 0x3DF2, 0x4EE3, { 0xB8, 0xD1, 0x86, 0x95, 0xF4, 0x57, 0xD3, 0xC1 }
  // compare with __declspec(uuid(...)) for the struct above.
};
template <>
constexpr auto
__mingw_uuidof<winrt::IDirect3DDxgiInterfaceAccess>() -> GUID const & {
  return GUID__IDirect3DDxgiInterfaceAccess;
}
#endif

namespace platf::dxgi {
  static HRESULT
  create_item_for_monitor(HMONITOR mon, winrt::GraphicsCaptureItem &item) {
    auto interop_factory = winrt::get_activation_factory<winrt::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    return interop_factory->CreateForMonitor(mon, winrt::guid_of<winrt::Windows::Graphics::Capture::IGraphicsCaptureItem>(), winrt::put_abi(item));
  }

  uwp_capture_t::uwp_capture_t() {
    InitializeCriticalSection(&frame_lock);
    InitializeConditionVariable(&frame_present_cv);
  }

  uwp_capture_t::~uwp_capture_t() {
    capture_session.Close();
    frame_pool.Close();
    item = nullptr;
    capture_session = nullptr;
    frame_pool = nullptr;
  }

  int
  uwp_capture_t::init(display_base_t *display, const ::video::config_t &config) {
    HRESULT status;
    dxgi::dxgi_t dxgi;
    winrt::com_ptr<::IInspectable> d3d_comhandle;
    if (!winrt::GraphicsCaptureSession::IsSupported()) {
      BOOST_LOG(error) << "Screen capture is not supported on this device for this release of Windows!"sv;
      return -1;
    }
    if (FAILED(status = display->device->QueryInterface(IID_IDXGIDevice, (void **) &dxgi))) {
      BOOST_LOG(error) << "Failed to query DXGI interface from device [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    if (FAILED(status = winrt::CreateDirect3D11DeviceFromDXGIDevice(*&dxgi, d3d_comhandle.put()))) {
      BOOST_LOG(error) << "Failed to query WinRT DirectX interface from device [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    uwp_device = d3d_comhandle.as<winrt::IDirect3DDevice>();
    DXGI_OUTPUT_DESC output_desc;
    display->output->GetDesc(&output_desc);
    if (FAILED(status = create_item_for_monitor(output_desc.Monitor, item))) {
      BOOST_LOG(error) << "Failed to activate GraphicsCaptureItem for monitor [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
    if (config.dynamicRange && display->is_hdr())
      display->capture_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    else
      display->capture_format = DXGI_FORMAT_B8G8R8A8_UNORM;

    frame_pool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(uwp_device, static_cast<winrt::Windows::Graphics::DirectX::DirectXPixelFormat>(display->capture_format), 2, item.Size());
    capture_session = frame_pool.CreateCaptureSession(item);
    frame_pool.FrameArrived({ this, &uwp_capture_t::on_frame_arrived });
    capture_session.IsBorderRequired(false);
    capture_session.IsCursorCaptureEnabled(true);
    capture_session.StartCapture();
    return 0;
  }

  void
  uwp_capture_t::on_frame_arrived(winrt::Direct3D11CaptureFramePool const &sender, winrt::IInspectable const &) {
    // this PRODUCER runs in a separate thread spawned by the frame pool. to
    // retain parity with the original API, the frame will be consumed by
    // the capture thread, not this one.

    auto frame = sender.TryGetNextFrame();
    if (frame != nullptr) {
      EnterCriticalSection(&frame_lock);
      capture_frame = frame;
      LeaveCriticalSection(&frame_lock);
      WakeConditionVariable(&frame_present_cv);
    }
  }

  capture_e
  uwp_capture_t::next_frame(std::chrono::milliseconds timeout, ID3D11Texture2D **out, uint64_t &out_time) {
    // this CONSUMER runs in the capture thread
    EnterCriticalSection(&frame_lock);
    SleepConditionVariableCS(&frame_present_cv, &frame_lock, timeout.count());
    if (capture_frame == nullptr) {
      LeaveCriticalSection(&frame_lock);
      return capture_e::timeout;
    }

    auto capture_access = capture_frame.Surface().as<winrt::IDirect3DDxgiInterfaceAccess>();
    capture_access->GetInterface(IID_ID3D11Texture2D, (void **) out);
    out_time = capture_frame.SystemRelativeTime().count();  // raw ticks from query performance counter
    LeaveCriticalSection(&frame_lock);
    return capture_e::ok;
  }

  capture_e
  uwp_capture_t::release_frame() {
    if (capture_frame != nullptr) {
      EnterCriticalSection(&frame_lock);
      capture_frame.Close();
      capture_frame = nullptr;
      LeaveCriticalSection(&frame_lock);
    }
    return capture_e::ok;
  }

  int
  display_uwp_ram_t::init(const ::video::config_t &config, const std::string &display_name) {
    if (display_base_t::init(config, display_name) || dup.init(this, config))
      return -1;

    texture.reset();
    return 0;
  }

  capture_e
  display_uwp_ram_t::snapshot(const pull_free_image_cb_t &pull_free_image_cb, std::shared_ptr<platf::img_t> &img_out, std::chrono::milliseconds timeout, bool cursor_visible) {
    HRESULT status;
    texture2d_t src;
    uint64_t frame_qpc;
    auto capture_status = dup.next_frame(timeout, &src, frame_qpc);
    if (capture_status != capture_e::ok)
      return capture_status;

    auto frame_timestamp = std::chrono::steady_clock::now() - qpc_time_difference(qpc_counter(), frame_qpc);
    D3D11_TEXTURE2D_DESC desc;
    src->GetDesc(&desc);

    // Create the staging texture if it doesn't exist. It should match the source in size and format.
    if (texture == nullptr) {
      capture_format = desc.Format;
      BOOST_LOG(info) << "Capture format ["sv << dxgi_format_to_string(capture_format) << ']';

      D3D11_TEXTURE2D_DESC t {};
      t.Width = width;
      t.Height = height;
      t.MipLevels = 1;
      t.ArraySize = 1;
      t.SampleDesc.Count = 1;
      t.Usage = D3D11_USAGE_STAGING;
      t.Format = capture_format;
      t.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

      auto status = device->CreateTexture2D(&t, nullptr, &texture);

      if (FAILED(status)) {
        BOOST_LOG(error) << "Failed to create staging texture [0x"sv << util::hex(status).to_string_view() << ']';
        return capture_e::error;
      }
    }

    // It's possible for our display enumeration to race with mode changes and result in
    // mismatched image pool and desktop texture sizes. If this happens, just reinit again.
    if (desc.Width != width || desc.Height != height) {
      BOOST_LOG(info) << "Capture size changed ["sv << width << 'x' << height << " -> "sv << desc.Width << 'x' << desc.Height << ']';
      return capture_e::reinit;
    }
    // It's also possible for the capture format to change on the fly. If that happens,
    // reinitialize capture to try format detection again and create new images.
    if (capture_format != desc.Format) {
      BOOST_LOG(info) << "Capture format changed ["sv << dxgi_format_to_string(capture_format) << " -> "sv << dxgi_format_to_string(desc.Format) << ']';
      return capture_e::reinit;
    }

    // Copy from GPU to CPU
    device_ctx->CopyResource(texture.get(), src.get());

    if (!pull_free_image_cb(img_out)) {
      return capture_e::interrupted;
    }
    auto img = (img_t *) img_out.get();

    // Map the staging texture for CPU access (making it inaccessible for the GPU)
    if (FAILED(status = device_ctx->Map(texture.get(), 0, D3D11_MAP_READ, 0, &img_info))) {
      BOOST_LOG(error) << "Failed to map texture [0x"sv << util::hex(status).to_string_view() << ']';

      return capture_e::error;
    }

    // Now that we know the capture format, we can finish creating the image
    if (complete_img(img, false)) {
      device_ctx->Unmap(texture.get(), 0);
      img_info.pData = nullptr;
      return capture_e::error;
    }

    std::copy_n((std::uint8_t *) img_info.pData, height * img_info.RowPitch, (std::uint8_t *) img->data);

    // Unmap the staging texture to allow GPU access again
    device_ctx->Unmap(texture.get(), 0);
    img_info.pData = nullptr;

    if (img) {
      img->frame_timestamp = frame_timestamp;
    }

    return capture_e::ok;
  }

  capture_e
  display_uwp_ram_t::release_snapshot() {
    return dup.release_frame();
  }
}  // namespace platf::dxgi
