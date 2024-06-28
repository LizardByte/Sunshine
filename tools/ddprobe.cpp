/**
 * @file tools/ddprobe.cpp
 * @brief Handles probing for DXGI duplication support.
 */
#include <d3d11.h>
#include <dxgi1_2.h>

#include <codecvt>
#include <iostream>
#include <locale>
#include <string>

#include "src/utility.h"

using namespace std::literals;
namespace dxgi {
  template <class T>
  void
  Release(T *dxgi) {
    dxgi->Release();
  }

  using factory1_t = util::safe_ptr<IDXGIFactory1, Release<IDXGIFactory1>>;
  using adapter_t = util::safe_ptr<IDXGIAdapter1, Release<IDXGIAdapter1>>;
  using output_t = util::safe_ptr<IDXGIOutput, Release<IDXGIOutput>>;
  using output1_t = util::safe_ptr<IDXGIOutput1, Release<IDXGIOutput1>>;
  using device_t = util::safe_ptr<ID3D11Device, Release<ID3D11Device>>;
  using dup_t = util::safe_ptr<IDXGIOutputDuplication, Release<IDXGIOutputDuplication>>;

}  // namespace dxgi

LSTATUS
set_gpu_preference(int preference) {
  // The GPU preferences key uses app path as the value name.
  WCHAR executable_path[MAX_PATH];
  GetModuleFileNameW(NULL, executable_path, ARRAYSIZE(executable_path));

  WCHAR value_data[128];
  swprintf_s(value_data, L"GpuPreference=%d;", preference);

  auto status = RegSetKeyValueW(HKEY_CURRENT_USER,
    L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
    executable_path,
    REG_SZ,
    value_data,
    (wcslen(value_data) + 1) * sizeof(WCHAR));
  if (status != ERROR_SUCCESS) {
    std::cout << "Failed to set GPU preference: "sv << status << std::endl;
    return status;
  }

  return ERROR_SUCCESS;
}

void
syncThreadDesktop() {
  auto hDesk = OpenInputDesktop(DF_ALLOWOTHERACCOUNTHOOK, FALSE, GENERIC_ALL);
  if (!hDesk) {
    auto err = GetLastError();
    std::cout << "Failed to Open Input Desktop [0x"sv << util::hex(err).to_string_view() << ']' << std::endl;
    return;
  }

  if (!SetThreadDesktop(hDesk)) {
    auto err = GetLastError();
    std::cout << "Failed to sync desktop to thread [0x"sv << util::hex(err).to_string_view() << ']' << std::endl;
  }

  CloseDesktop(hDesk);
}

HRESULT
test_dxgi_duplication(dxgi::adapter_t &adapter, dxgi::output_t &output) {
  D3D_FEATURE_LEVEL featureLevels[] {
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1
  };

  dxgi::device_t device;
  auto status = D3D11CreateDevice(
    adapter.get(),
    D3D_DRIVER_TYPE_UNKNOWN,
    nullptr,
    D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
    featureLevels, sizeof(featureLevels) / sizeof(D3D_FEATURE_LEVEL),
    D3D11_SDK_VERSION,
    &device,
    nullptr,
    nullptr);
  if (FAILED(status)) {
    std::cout << "Failed to create D3D11 device for DD test [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
    return status;
  }

  dxgi::output1_t output1;
  status = output->QueryInterface(IID_IDXGIOutput1, (void **) &output1);
  if (FAILED(status)) {
    std::cout << "Failed to query IDXGIOutput1 from the output"sv << std::endl;
    return status;
  }

  // Ensure we can duplicate the current display
  syncThreadDesktop();

  // Return the result of DuplicateOutput() to Sunshine
  dxgi::dup_t dup;
  return output1->DuplicateOutput((IUnknown *) device.get(), &dup);
}

int
main(int argc, char *argv[]) {
  HRESULT status;

  // Display name may be omitted
  if (argc != 2 && argc != 3) {
    std::cout << "ddprobe.exe [GPU preference value] [display name]"sv << std::endl;
    return -1;
  }

  std::wstring display_name;
  if (argc == 3) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
    display_name = converter.from_bytes(argv[2]);
  }

  // We must set the GPU preference before making any DXGI/D3D calls
  status = set_gpu_preference(atoi(argv[1]));
  if (status != ERROR_SUCCESS) {
    return status;
  }

  // Remove the GPU preference when we're done
  auto reset_gpu = util::fail_guard([]() {
    WCHAR tool_path[MAX_PATH];
    GetModuleFileNameW(NULL, tool_path, ARRAYSIZE(tool_path));

    RegDeleteKeyValueW(HKEY_CURRENT_USER,
      L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
      tool_path);
  });

  dxgi::factory1_t factory;
  status = CreateDXGIFactory1(IID_IDXGIFactory1, (void **) &factory);
  if (FAILED(status)) {
    std::cout << "Failed to create DXGIFactory1 [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
    return status;
  }

  dxgi::adapter_t::pointer adapter_p {};
  for (int x = 0; factory->EnumAdapters1(x, &adapter_p) != DXGI_ERROR_NOT_FOUND; ++x) {
    dxgi::adapter_t adapter { adapter_p };

    dxgi::output_t::pointer output_p {};
    for (int y = 0; adapter->EnumOutputs(y, &output_p) != DXGI_ERROR_NOT_FOUND; ++y) {
      dxgi::output_t output { output_p };

      DXGI_OUTPUT_DESC desc;
      output->GetDesc(&desc);

      // If a display name was specified and this one doesn't match, skip it
      if (!display_name.empty() && desc.DeviceName != display_name) {
        continue;
      }

      // If this display is not part of the desktop, we definitely can't capture it
      if (!desc.AttachedToDesktop) {
        continue;
      }

      // We found the matching output. Test it and return the result.
      return test_dxgi_duplication(adapter, output);
    }
  }

  return 0;
}
