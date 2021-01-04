//
// Created by loki on 1/12/20.
//

#include <codecvt>

#include "sunshine/config.h"
#include "sunshine/main.h"
#include "sunshine/platform/common.h"

#include "display.h"

namespace platf {
using namespace std::literals;
}
namespace platf::dxgi {
capture_e duplication_t::next_frame(DXGI_OUTDUPL_FRAME_INFO &frame_info, std::chrono::milliseconds timeout, resource_t::pointer *res_p) {
  auto capture_status = release_frame();
  if(capture_status != capture_e::ok) {
    return capture_status;
  }

  auto status = dup->AcquireNextFrame(timeout.count(), &frame_info, res_p);

  switch(status) {
    case S_OK:
      has_frame = true;
      return capture_e::ok;
    case DXGI_ERROR_WAIT_TIMEOUT:
      return capture_e::timeout;
    case WAIT_ABANDONED:
    case DXGI_ERROR_ACCESS_LOST:
    case DXGI_ERROR_ACCESS_DENIED:
      return capture_e::reinit;
    default:
      BOOST_LOG(error) << "Couldn't acquire next frame [0x"sv << util::hex(status).to_string_view();
      return capture_e::error;
  }
}

capture_e duplication_t::reset(dup_t::pointer dup_p) {
  auto capture_status = release_frame();

  dup.reset(dup_p);

  return capture_status;
}

capture_e duplication_t::release_frame() {
  if(!has_frame) {
    return capture_e::ok;
  }

  auto status = dup->ReleaseFrame();
  switch (status) {
    case S_OK:
      has_frame = false;
      return capture_e::ok;
    case DXGI_ERROR_WAIT_TIMEOUT:
      return capture_e::timeout;
    case WAIT_ABANDONED:
    case DXGI_ERROR_ACCESS_LOST:
    case DXGI_ERROR_ACCESS_DENIED:
      has_frame = false;
      return capture_e::reinit;
    default:
      BOOST_LOG(error) << "Couldn't release frame [0x"sv << util::hex(status).to_string_view();
      return capture_e::error;
  }
}

duplication_t::~duplication_t() {
  release_frame();
}

int display_base_t::init() {
/* Uncomment when use of IDXGIOutput5 is implemented
  std::call_once(windows_cpp_once_flag, []() {
    DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
    const auto DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = ((DPI_AWARENESS_CONTEXT)-4);

    typedef BOOL (*User32_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT value);

    auto user32 = LoadLibraryA("user32.dll");
    auto f = (User32_SetProcessDpiAwarenessContext)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if(f) {
      f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    FreeLibrary(user32);
  });
*/
  dxgi::factory1_t::pointer   factory_p {};
  dxgi::adapter_t::pointer    adapter_p {};
  dxgi::output_t::pointer     output_p {};
  dxgi::device_t::pointer     device_p {};
  dxgi::device_ctx_t::pointer device_ctx_p {};

  HRESULT status;

  status = CreateDXGIFactory1(IID_IDXGIFactory1, (void**)&factory_p);
  factory.reset(factory_p);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create DXGIFactory1 [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;

  auto adapter_name = converter.from_bytes(config::video.adapter_name);
  auto output_name = converter.from_bytes(config::video.output_name);

  for(int x = 0; factory_p->EnumAdapters1(x, &adapter_p) != DXGI_ERROR_NOT_FOUND; ++x) {
    dxgi::adapter_t adapter_tmp { adapter_p };

    DXGI_ADAPTER_DESC1 adapter_desc;
    adapter_tmp->GetDesc1(&adapter_desc);

    if(!adapter_name.empty() && adapter_desc.Description != adapter_name) {
      continue;
    }

    for(int y = 0; adapter_tmp->EnumOutputs(y, &output_p) != DXGI_ERROR_NOT_FOUND; ++y) {
      dxgi::output_t output_tmp {output_p };

      DXGI_OUTPUT_DESC desc;
      output_tmp->GetDesc(&desc);

      if(!output_name.empty() && desc.DeviceName != output_name) {
        continue;
      }

      if(desc.AttachedToDesktop) {
        output = std::move(output_tmp);

        width  = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
        height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
      }
    }

    if(output) {
      adapter = std::move(adapter_tmp);
      break;
    }
  }

  if(!output) {
    BOOST_LOG(error) << "Failed to locate an output device"sv;
    return -1;
  }

  D3D_FEATURE_LEVEL featureLevels[] {
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1
  };

  status = adapter->QueryInterface(IID_IDXGIAdapter, (void**)&adapter_p);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to query IDXGIAdapter interface"sv;

    return -1;
  }

  status = D3D11CreateDevice(
    adapter_p,
    D3D_DRIVER_TYPE_UNKNOWN,
    nullptr,
    D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
    featureLevels, sizeof(featureLevels) / sizeof(D3D_FEATURE_LEVEL),
    D3D11_SDK_VERSION,
    &device_p,
    &feature_level,
    &device_ctx_p);

  adapter_p->Release();

  device.reset(device_p);
  device_ctx.reset(device_ctx_p);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create D3D11 device [0x"sv << util::hex(status).to_string_view() << ']';

    return -1;
  }

  DXGI_ADAPTER_DESC adapter_desc;
  adapter->GetDesc(&adapter_desc);

  auto description = converter.to_bytes(adapter_desc.Description);
  BOOST_LOG(info)
    << std::endl
    << "Device Description : " << description << std::endl
    << "Device Vendor ID   : 0x"sv << util::hex(adapter_desc.VendorId).to_string_view() << std::endl
    << "Device Device ID   : 0x"sv << util::hex(adapter_desc.DeviceId).to_string_view() << std::endl
    << "Device Video Mem   : "sv << adapter_desc.DedicatedVideoMemory / 1048576 << " MiB"sv << std::endl
    << "Device Sys Mem     : "sv << adapter_desc.DedicatedSystemMemory / 1048576 << " MiB"sv << std::endl
    << "Share Sys Mem      : "sv << adapter_desc.SharedSystemMemory / 1048576 << " MiB"sv << std::endl
    << "Feature Level      : 0x"sv << util::hex(feature_level).to_string_view() << std::endl
    << "Capture size       : "sv << width << 'x'  << height;

  // Bump up thread priority
  {
    const DWORD flags = TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY;
    TOKEN_PRIVILEGES tp;
    HANDLE token;
    LUID val;

    if (OpenProcessToken(GetCurrentProcess(), flags, &token) &&
       !!LookupPrivilegeValue(NULL, SE_INC_BASE_PRIORITY_NAME, &val)) {
      tp.PrivilegeCount = 1;
      tp.Privileges[0].Luid = val;
      tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

      if (!AdjustTokenPrivileges(token, false, &tp, sizeof(tp), NULL, NULL)) {
        BOOST_LOG(error) << "Could not set privilege to increase GPU priority";
      }
    }

    CloseHandle(token);

    HMODULE gdi32 = GetModuleHandleA("GDI32");
    if (gdi32) {
      PD3DKMTSetProcessSchedulingPriorityClass fn =
        (PD3DKMTSetProcessSchedulingPriorityClass)GetProcAddress(gdi32, "D3DKMTSetProcessSchedulingPriorityClass");
      if (fn) {
        status = fn(GetCurrentProcess(), D3DKMT_SCHEDULINGPRIORITYCLASS_REALTIME);
        if (FAILED(status)) {
          BOOST_LOG(error) << "Failed to set realtime GPU priority. Please run application as administrator for optimal performance.";
        }
      }
    }

    dxgi::dxgi_t::pointer dxgi_p {};
    status = device->QueryInterface(IID_IDXGIDevice, (void**)&dxgi_p);
    dxgi::dxgi_t dxgi { dxgi_p };

    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to query DXGI interface from device [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    dxgi->SetGPUThreadPriority(7);
  }

  // Try to reduce latency
  {
    dxgi::dxgi1_t::pointer dxgi_p {};
    status = device->QueryInterface(IID_IDXGIDevice, (void**)&dxgi_p);
    dxgi::dxgi1_t dxgi { dxgi_p };

    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to query DXGI interface from device [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    dxgi->SetMaximumFrameLatency(1);
  }

  //FIXME: Duplicate output on RX580 in combination with DOOM (2016) --> BSOD
  //TODO: Use IDXGIOutput5 for improved performance
  {
    dxgi::output1_t::pointer output1_p {};
    status = output->QueryInterface(IID_IDXGIOutput1, (void**)&output1_p);
    dxgi::output1_t output1 {output1_p };

    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to query IDXGIOutput1 from the output"sv;
      return -1;
    }

    // We try this twice, in case we still get an error on reinitialization
    for(int x = 0; x < 2; ++x) {
      dxgi::dup_t::pointer dup_p {};
      status = output1->DuplicateOutput((IUnknown*)device.get(), &dup_p);
      if(SUCCEEDED(status)) {
        dup.reset(dup_p);
        break;
      }
      std::this_thread::sleep_for(200ms);
    }

    if(FAILED(status)) {
      BOOST_LOG(error) << "DuplicateOutput Failed [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }
  }

  DXGI_OUTDUPL_DESC dup_desc;
  dup.dup->GetDesc(&dup_desc);

  format = dup_desc.ModeDesc.Format;

  BOOST_LOG(debug) << "Source format ["sv << format_str[dup_desc.ModeDesc.Format] << ']';

  return 0;
}

const char *format_str[] = {
  "DXGI_FORMAT_UNKNOWN",
  "DXGI_FORMAT_R32G32B32A32_TYPELESS",
  "DXGI_FORMAT_R32G32B32A32_FLOAT",
  "DXGI_FORMAT_R32G32B32A32_UINT",
  "DXGI_FORMAT_R32G32B32A32_SINT",
  "DXGI_FORMAT_R32G32B32_TYPELESS",
  "DXGI_FORMAT_R32G32B32_FLOAT",
  "DXGI_FORMAT_R32G32B32_UINT",
  "DXGI_FORMAT_R32G32B32_SINT",
  "DXGI_FORMAT_R16G16B16A16_TYPELESS",
  "DXGI_FORMAT_R16G16B16A16_FLOAT",
  "DXGI_FORMAT_R16G16B16A16_UNORM",
  "DXGI_FORMAT_R16G16B16A16_UINT",
  "DXGI_FORMAT_R16G16B16A16_SNORM",
  "DXGI_FORMAT_R16G16B16A16_SINT",
  "DXGI_FORMAT_R32G32_TYPELESS",
  "DXGI_FORMAT_R32G32_FLOAT",
  "DXGI_FORMAT_R32G32_UINT",
  "DXGI_FORMAT_R32G32_SINT",
  "DXGI_FORMAT_R32G8X24_TYPELESS",
  "DXGI_FORMAT_D32_FLOAT_S8X24_UINT",
  "DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS",
  "DXGI_FORMAT_X32_TYPELESS_G8X24_UINT",
  "DXGI_FORMAT_R10G10B10A2_TYPELESS",
  "DXGI_FORMAT_R10G10B10A2_UNORM",
  "DXGI_FORMAT_R10G10B10A2_UINT",
  "DXGI_FORMAT_R11G11B10_FLOAT",
  "DXGI_FORMAT_R8G8B8A8_TYPELESS",
  "DXGI_FORMAT_R8G8B8A8_UNORM",
  "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB",
  "DXGI_FORMAT_R8G8B8A8_UINT",
  "DXGI_FORMAT_R8G8B8A8_SNORM",
  "DXGI_FORMAT_R8G8B8A8_SINT",
  "DXGI_FORMAT_R16G16_TYPELESS",
  "DXGI_FORMAT_R16G16_FLOAT",
  "DXGI_FORMAT_R16G16_UNORM",
  "DXGI_FORMAT_R16G16_UINT",
  "DXGI_FORMAT_R16G16_SNORM",
  "DXGI_FORMAT_R16G16_SINT",
  "DXGI_FORMAT_R32_TYPELESS",
  "DXGI_FORMAT_D32_FLOAT",
  "DXGI_FORMAT_R32_FLOAT",
  "DXGI_FORMAT_R32_UINT",
  "DXGI_FORMAT_R32_SINT",
  "DXGI_FORMAT_R24G8_TYPELESS",
  "DXGI_FORMAT_D24_UNORM_S8_UINT",
  "DXGI_FORMAT_R24_UNORM_X8_TYPELESS",
  "DXGI_FORMAT_X24_TYPELESS_G8_UINT",
  "DXGI_FORMAT_R8G8_TYPELESS",
  "DXGI_FORMAT_R8G8_UNORM",
  "DXGI_FORMAT_R8G8_UINT",
  "DXGI_FORMAT_R8G8_SNORM",
  "DXGI_FORMAT_R8G8_SINT",
  "DXGI_FORMAT_R16_TYPELESS",
  "DXGI_FORMAT_R16_FLOAT",
  "DXGI_FORMAT_D16_UNORM",
  "DXGI_FORMAT_R16_UNORM",
  "DXGI_FORMAT_R16_UINT",
  "DXGI_FORMAT_R16_SNORM",
  "DXGI_FORMAT_R16_SINT",
  "DXGI_FORMAT_R8_TYPELESS",
  "DXGI_FORMAT_R8_UNORM",
  "DXGI_FORMAT_R8_UINT",
  "DXGI_FORMAT_R8_SNORM",
  "DXGI_FORMAT_R8_SINT",
  "DXGI_FORMAT_A8_UNORM",
  "DXGI_FORMAT_R1_UNORM",
  "DXGI_FORMAT_R9G9B9E5_SHAREDEXP",
  "DXGI_FORMAT_R8G8_B8G8_UNORM",
  "DXGI_FORMAT_G8R8_G8B8_UNORM",
  "DXGI_FORMAT_BC1_TYPELESS",
  "DXGI_FORMAT_BC1_UNORM",
  "DXGI_FORMAT_BC1_UNORM_SRGB",
  "DXGI_FORMAT_BC2_TYPELESS",
  "DXGI_FORMAT_BC2_UNORM",
  "DXGI_FORMAT_BC2_UNORM_SRGB",
  "DXGI_FORMAT_BC3_TYPELESS",
  "DXGI_FORMAT_BC3_UNORM",
  "DXGI_FORMAT_BC3_UNORM_SRGB",
  "DXGI_FORMAT_BC4_TYPELESS",
  "DXGI_FORMAT_BC4_UNORM",
  "DXGI_FORMAT_BC4_SNORM",
  "DXGI_FORMAT_BC5_TYPELESS",
  "DXGI_FORMAT_BC5_UNORM",
  "DXGI_FORMAT_BC5_SNORM",
  "DXGI_FORMAT_B5G6R5_UNORM",
  "DXGI_FORMAT_B5G5R5A1_UNORM",
  "DXGI_FORMAT_B8G8R8A8_UNORM",
  "DXGI_FORMAT_B8G8R8X8_UNORM",
  "DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM",
  "DXGI_FORMAT_B8G8R8A8_TYPELESS",
  "DXGI_FORMAT_B8G8R8A8_UNORM_SRGB",
  "DXGI_FORMAT_B8G8R8X8_TYPELESS",
  "DXGI_FORMAT_B8G8R8X8_UNORM_SRGB",
  "DXGI_FORMAT_BC6H_TYPELESS",
  "DXGI_FORMAT_BC6H_UF16",
  "DXGI_FORMAT_BC6H_SF16",
  "DXGI_FORMAT_BC7_TYPELESS",
  "DXGI_FORMAT_BC7_UNORM",
  "DXGI_FORMAT_BC7_UNORM_SRGB",
  "DXGI_FORMAT_AYUV",
  "DXGI_FORMAT_Y410",
  "DXGI_FORMAT_Y416",
  "DXGI_FORMAT_NV12",
  "DXGI_FORMAT_P010",
  "DXGI_FORMAT_P016",
  "DXGI_FORMAT_420_OPAQUE",
  "DXGI_FORMAT_YUY2",
  "DXGI_FORMAT_Y210",
  "DXGI_FORMAT_Y216",
  "DXGI_FORMAT_NV11",
  "DXGI_FORMAT_AI44",
  "DXGI_FORMAT_IA44",
  "DXGI_FORMAT_P8",
  "DXGI_FORMAT_A8P8",
  "DXGI_FORMAT_B4G4R4A4_UNORM",

  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,

  "DXGI_FORMAT_P208",
  "DXGI_FORMAT_V208",
  "DXGI_FORMAT_V408"
};

}

namespace platf {
std::shared_ptr<display_t> display(dev_type_e hwdevice_type) {
  if(hwdevice_type == dev_type_e::dxgi) {
    auto disp = std::make_shared<dxgi::display_vram_t>();

    if(!disp->init()) {
      return disp;
    }
  }
  else if(hwdevice_type == dev_type_e::none) {
    auto disp = std::make_shared<dxgi::display_ram_t>();

    if(!disp->init()) {
      return disp;
    }
  }

  return nullptr;
}
}
