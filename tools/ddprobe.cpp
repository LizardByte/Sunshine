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
#include <wrl.h>

#include "src/utility.h"

using Microsoft::WRL::ComPtr;
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

/**
  * @brief Determines if a given frame is valid by checking if it contains any non-dark pixels.
  *
  * This function analyzes the provided frame to determine if it contains any pixels that exceed a specified darkness threshold.
  * It iterates over all pixels in the frame, comparing each pixel's RGB values to the defined darkness threshold.
  * If any pixel's RGB values exceed this threshold, the function concludes that the frame is valid (i.e., not entirely dark) and returns `true`.
  * If all pixels are below or equal to the threshold, indicating a completely dark frame, the function returns `false`.

  * @param mappedResource A reference to a `D3D11_MAPPED_SUBRESOURCE` structure containing the mapped subresource data of the frame to be analyzed.
  * @param frameDesc A reference to a `D3D11_TEXTURE2D_DESC` structure describing the texture properties, including width and height.
  * @param darknessThreshold A floating-point value representing the threshold above which a pixel's RGB values are considered dark. The value ranges from 0.0f to 1.0f, with a default value of 0.1f.
  * @return Returns `true` if the frame contains any non-dark pixels, indicating it is valid; otherwise, returns `false`.
  */
bool
is_valid_frame(const D3D11_MAPPED_SUBRESOURCE &mappedResource, const D3D11_TEXTURE2D_DESC &frameDesc, float darknessThreshold = 0.1f) {
  const auto *pixels = static_cast<const uint8_t *>(mappedResource.pData);
  const int bytesPerPixel = 4;  // (8 bits per channel, excluding alpha). Factoring HDR is not needed because it doesn't cause black levels to raise enough to be a concern.
  const int stride = mappedResource.RowPitch;
  const int width = frameDesc.Width;
  const int height = frameDesc.Height;

  // Convert the darkness threshold to an integer value for comparison
  const auto threshold = static_cast<int>(darknessThreshold * 255);

  // Iterate over each pixel in the frame
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint8_t *pixel = pixels + y * stride + x * bytesPerPixel;
      // Check if any RGB channel exceeds the darkness threshold
      if (pixel[0] > threshold || pixel[1] > threshold || pixel[2] > threshold) {
        // Frame is not dark
        return true;
      }
    }
  }
  // Frame is entirely dark
  return false;
}

/**
 * @brief Captures and verifies the contents of up to 10 consecutive frames from a DXGI output duplication.
 *
 * This function attempts to acquire and analyze up to 10 frames from a DXGI output duplication object (`dup`).
 * It checks if each frame is non-empty (not entirely dark) by using the `is_valid_frame` function.
 * If any non-empty frame is found, the function returns `S_OK`.
 * If all 10 frames are empty, it returns `E_FAIL`, suggesting potential issues with the capture process.
 * If any error occurs during the frame acquisition or analysis process, the corresponding `HRESULT` error code is returned.
 *
 * @param dup A reference to the DXGI output duplication object (`dxgi::dup_t&`) used to acquire frames.
 * @param device A ComPtr to the ID3D11Device interface representing the device associated with the Direct3D context.
 * @return Returns `S_OK` if a non-empty frame is captured successfully, `E_FAIL` if all frames are empty, or an error code if any failure occurs during the process.
 */
HRESULT
test_frame_capture(dxgi::dup_t &dup, ComPtr<ID3D11Device> device) {
  for (int i = 0; i < 10; ++i) {
    std::cout << "Attempting to acquire frame " << (i + 1) << " of 10..." << std::endl;
    ComPtr<IDXGIResource> frameResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> stagingTexture;

    HRESULT status = dup->AcquireNextFrame(500, &frameInfo, &frameResource);
    device->GetImmediateContext(&context);

    if (FAILED(status)) {
      std::cout << "Error: Failed to acquire next frame [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
      return status;
    }

    auto cleanup = util::fail_guard([&dup]() {
      dup->ReleaseFrame();
    });

    std::cout << "Frame acquired successfully." << std::endl;

    ComPtr<ID3D11Texture2D> frameTexture;
    status = frameResource->QueryInterface(IID_PPV_ARGS(&frameTexture));
    if (FAILED(status)) {
      std::cout << "Error: Failed to query texture interface from frame resource [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
      return status;
    }

    D3D11_TEXTURE2D_DESC frameDesc;
    frameTexture->GetDesc(&frameDesc);
    frameDesc.Usage = D3D11_USAGE_STAGING;
    frameDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    frameDesc.BindFlags = 0;
    frameDesc.MiscFlags = 0;

    status = device->CreateTexture2D(&frameDesc, nullptr, &stagingTexture);
    if (FAILED(status)) {
      std::cout << "Error: Failed to create staging texture [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
      return status;
    }

    context->CopyResource(stagingTexture.Get(), frameTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    status = context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(status)) {
      std::cout << "Error: Failed to map the staging texture for inspection [0x"sv << util::hex(status).to_string_view() << ']' << std::endl;
      return status;
    }

    auto contextCleanup = util::fail_guard([&context, &stagingTexture]() {
      context->Unmap(stagingTexture.Get(), 0);
    });

    if (is_valid_frame(mappedResource, frameDesc)) {
      std::cout << "Frame " << (i + 1) << " is non-empty (contains visible content)." << std::endl;
      return S_OK;
    }

    std::cout << "Frame " << (i + 1) << " is empty (no visible content)." << std::endl;
  }

  // All frames were empty, indicating potential capture issues.
  return E_FAIL;
}

HRESULT
test_dxgi_duplication(dxgi::adapter_t &adapter, dxgi::output_t &output, bool verify_frame_capture) {
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

  // Attempt to duplicate the output
  dxgi::dup_t dup;
  ComPtr<ID3D11Device> device_ptr(device.get());
  HRESULT result = output1->DuplicateOutput(device_ptr.Get(), &dup);

  if (FAILED(result)) {
    std::cout << "Failed to duplicate output [0x"sv << util::hex(result).to_string_view() << "]" << std::endl;
    return result;
  }

  // To prevent false negatives, we'll make it optional to test for frame capture.
  if (verify_frame_capture) {
    HRESULT captureResult = test_frame_capture(dup, device_ptr.Get());
    if (FAILED(captureResult)) {
      std::cout << "Frame capture test failed [0x"sv << util::hex(captureResult).to_string_view() << "]" << std::endl;
      return captureResult;
    }
  }

  return S_OK;
}

int
main(int argc, char *argv[]) {
  HRESULT status;

  // Usage message
  if (argc < 2 || argc > 4) {
    std::cout << "Usage: ddprobe.exe [GPU preference value] [display name] [--verify-frame-capture]"sv << std::endl;
    return -1;
  }

  std::wstring display_name;
  bool verify_frame_capture = false;

  // Parse GPU preference value (required)
  int gpu_preference = atoi(argv[1]);

  // Parse optional arguments
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--verify-frame-capture") {
      verify_frame_capture = true;
    }
    else {
      // Assume any other argument is the display name
      std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
      display_name = converter.from_bytes(arg);
    }
  }

  // We must set the GPU preference before making any DXGI/D3D calls
  status = set_gpu_preference(gpu_preference);
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
      return test_dxgi_duplication(adapter, output, verify_frame_capture);
    }
  }

  return 0;
}
