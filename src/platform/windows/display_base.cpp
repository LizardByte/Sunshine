//
// Created by loki on 1/12/20.
//

#include <cmath>
#include <codecvt>

#include "display.h"
#include "misc.h"
#include "src/config.h"
#include "src/main.h"
#include "src/platform/common.h"

namespace platf {
using namespace std::literals;
}
namespace platf::dxgi {
capture_e duplication_t::next_frame(DXGI_OUTDUPL_FRAME_INFO &frame_info, std::chrono::milliseconds timeout, resource_t::pointer *res_p) {
  auto capture_status = release_frame();
  if(capture_status != capture_e::ok) {
    return capture_status;
  }

  if(use_dwmflush) {
    DwmFlush();
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
  switch(status) {
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

int display_base_t::init(int framerate, const std::string &display_name) {
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

  // Ensure we can duplicate the current display
  syncThreadDesktop();

  delay = std::chrono::nanoseconds { 1s } / framerate;

  // Get rectangle of full desktop for absolute mouse coordinates
  env_width  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  env_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

  HRESULT status;

  status = CreateDXGIFactory1(IID_IDXGIFactory1, (void **)&factory);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create DXGIFactory1 [0x"sv << util::hex(status).to_string_view() << ']';
    return -1;
  }

  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;

  auto adapter_name = converter.from_bytes(config::video.adapter_name);
  auto output_name  = converter.from_bytes(display_name);

  adapter_t::pointer adapter_p;
  for(int x = 0; factory->EnumAdapters1(x, &adapter_p) != DXGI_ERROR_NOT_FOUND; ++x) {
    dxgi::adapter_t adapter_tmp { adapter_p };

    DXGI_ADAPTER_DESC1 adapter_desc;
    adapter_tmp->GetDesc1(&adapter_desc);

    if(!adapter_name.empty() && adapter_desc.Description != adapter_name) {
      continue;
    }

    dxgi::output_t::pointer output_p;
    for(int y = 0; adapter_tmp->EnumOutputs(y, &output_p) != DXGI_ERROR_NOT_FOUND; ++y) {
      dxgi::output_t output_tmp { output_p };

      DXGI_OUTPUT_DESC desc;
      output_tmp->GetDesc(&desc);

      if(!output_name.empty() && desc.DeviceName != output_name) {
        continue;
      }

      if(desc.AttachedToDesktop) {
        output = std::move(output_tmp);

        offset_x = desc.DesktopCoordinates.left;
        offset_y = desc.DesktopCoordinates.top;
        width    = desc.DesktopCoordinates.right - offset_x;
        height   = desc.DesktopCoordinates.bottom - offset_y;

        // left and bottom may be negative, yet absolute mouse coordinates start at 0x0
        // Ensure offset starts at 0x0
        offset_x -= GetSystemMetrics(SM_XVIRTUALSCREEN);
        offset_y -= GetSystemMetrics(SM_YVIRTUALSCREEN);
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
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1
  };

  status = adapter->QueryInterface(IID_IDXGIAdapter, (void **)&adapter_p);
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
    &device,
    &feature_level,
    &device_ctx);

  adapter_p->Release();

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
    << "Capture size       : "sv << width << 'x' << height << std::endl
    << "Offset             : "sv << offset_x << 'x' << offset_y << std::endl
    << "Virtual Desktop    : "sv << env_width << 'x' << env_height;

  // Enable DwmFlush() only if the current refresh rate can match the client framerate.
  auto refresh_rate = framerate;
  DWM_TIMING_INFO timing_info;
  timing_info.cbSize = sizeof(timing_info);

  status = DwmGetCompositionTimingInfo(NULL, &timing_info);
  if(FAILED(status)) {
    BOOST_LOG(warning) << "Failed to detect active refresh rate.";
  }
  else {
    refresh_rate = std::round((double)timing_info.rateRefresh.uiNumerator / (double)timing_info.rateRefresh.uiDenominator);
  }

  dup.use_dwmflush = config::video.dwmflush && !(framerate > refresh_rate) ? true : false;

  // Bump up thread priority
  {
    const DWORD flags = TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY;
    TOKEN_PRIVILEGES tp;
    HANDLE token;
    LUID val;

    if(OpenProcessToken(GetCurrentProcess(), flags, &token) &&
       !!LookupPrivilegeValue(NULL, SE_INC_BASE_PRIORITY_NAME, &val)) {
      tp.PrivilegeCount           = 1;
      tp.Privileges[0].Luid       = val;
      tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

      if(!AdjustTokenPrivileges(token, false, &tp, sizeof(tp), NULL, NULL)) {
        BOOST_LOG(warning) << "Could not set privilege to increase GPU priority";
      }
    }

    CloseHandle(token);

    HMODULE gdi32 = GetModuleHandleA("GDI32");
    if(gdi32) {
      PD3DKMTSetProcessSchedulingPriorityClass fn =
        (PD3DKMTSetProcessSchedulingPriorityClass)GetProcAddress(gdi32, "D3DKMTSetProcessSchedulingPriorityClass");
      if(fn) {
        status = fn(GetCurrentProcess(), D3DKMT_SCHEDULINGPRIORITYCLASS_REALTIME);
        if(FAILED(status)) {
          BOOST_LOG(warning) << "Failed to set realtime GPU priority. Please run application as administrator for optimal performance.";
        }
      }
    }

    dxgi::dxgi_t dxgi;
    status = device->QueryInterface(IID_IDXGIDevice, (void **)&dxgi);
    if(FAILED(status)) {
      BOOST_LOG(warning) << "Failed to query DXGI interface from device [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    dxgi->SetGPUThreadPriority(7);
  }

  // Try to reduce latency
  {
    dxgi::dxgi1_t dxgi {};
    status = device->QueryInterface(IID_IDXGIDevice, (void **)&dxgi);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to query DXGI interface from device [0x"sv << util::hex(status).to_string_view() << ']';
      return -1;
    }

    status = dxgi->SetMaximumFrameLatency(1);
    if(FAILED(status)) {
      BOOST_LOG(warning) << "Failed to set maximum frame latency [0x"sv << util::hex(status).to_string_view() << ']';
    }
  }

  //FIXME: Duplicate output on RX580 in combination with DOOM (2016) --> BSOD
  //TODO: Use IDXGIOutput5 for improved performance
  {
    dxgi::output1_t output1 {};
    status = output->QueryInterface(IID_IDXGIOutput1, (void **)&output1);
    if(FAILED(status)) {
      BOOST_LOG(error) << "Failed to query IDXGIOutput1 from the output"sv;
      return -1;
    }

    // We try this twice, in case we still get an error on reinitialization
    for(int x = 0; x < 2; ++x) {
      status = output1->DuplicateOutput((IUnknown *)device.get(), &dup.dup);
      if(SUCCEEDED(status)) {
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

} // namespace platf::dxgi

namespace platf {
std::shared_ptr<display_t> display(mem_type_e hwdevice_type, const std::string &display_name, int framerate) {
  if(hwdevice_type == mem_type_e::dxgi) {
    auto disp = std::make_shared<dxgi::display_vram_t>();

    if(!disp->init(framerate, display_name)) {
      return disp;
    }
  }
  else if(hwdevice_type == mem_type_e::system) {
    auto disp = std::make_shared<dxgi::display_ram_t>();

    if(!disp->init(framerate, display_name)) {
      return disp;
    }
  }

  return nullptr;
}

std::vector<std::string> display_names(mem_type_e) {
  std::vector<std::string> display_names;

  HRESULT status;

  BOOST_LOG(debug) << "Detecting monitors..."sv;

  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;

  dxgi::factory1_t factory;
  status = CreateDXGIFactory1(IID_IDXGIFactory1, (void **)&factory);
  if(FAILED(status)) {
    BOOST_LOG(error) << "Failed to create DXGIFactory1 [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
    return {};
  }

  dxgi::adapter_t adapter;
  for(int x = 0; factory->EnumAdapters1(x, &adapter) != DXGI_ERROR_NOT_FOUND; ++x) {
    DXGI_ADAPTER_DESC1 adapter_desc;
    adapter->GetDesc1(&adapter_desc);

    BOOST_LOG(debug)
      << std::endl
      << "====== ADAPTER ====="sv << std::endl
      << "Device Name      : "sv << converter.to_bytes(adapter_desc.Description) << std::endl
      << "Device Vendor ID : 0x"sv << util::hex(adapter_desc.VendorId).to_string_view() << std::endl
      << "Device Device ID : 0x"sv << util::hex(adapter_desc.DeviceId).to_string_view() << std::endl
      << "Device Video Mem : "sv << adapter_desc.DedicatedVideoMemory / 1048576 << " MiB"sv << std::endl
      << "Device Sys Mem   : "sv << adapter_desc.DedicatedSystemMemory / 1048576 << " MiB"sv << std::endl
      << "Share Sys Mem    : "sv << adapter_desc.SharedSystemMemory / 1048576 << " MiB"sv << std::endl
      << std::endl
      << "    ====== OUTPUT ======"sv << std::endl;

    dxgi::output_t::pointer output_p {};
    for(int y = 0; adapter->EnumOutputs(y, &output_p) != DXGI_ERROR_NOT_FOUND; ++y) {
      dxgi::output_t output { output_p };

      DXGI_OUTPUT_DESC desc;
      output->GetDesc(&desc);

      auto device_name = converter.to_bytes(desc.DeviceName);

      auto width  = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
      auto height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

      BOOST_LOG(debug)
        << "    Output Name       : "sv << device_name << std::endl
        << "    AttachedToDesktop : "sv << (desc.AttachedToDesktop ? "yes"sv : "no"sv) << std::endl
        << "    Resolution        : "sv << width << 'x' << height << std::endl
        << std::endl;

      display_names.emplace_back(std::move(device_name));
    }
  }

  return display_names;
}

} // namespace platf
