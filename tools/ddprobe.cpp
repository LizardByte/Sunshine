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

/**
 * @brief Determines whether a given frame is entirely black by checking every pixel.
 *
 * This function checks if the provided frame is entirely black by inspecting each pixel in both the x and y dimensions. It inspects the RGB channels of each pixel and compares them against a specified black threshold. If any pixel's RGB values exceed this threshold, the frame is considered not black, and the function returns `false`. Otherwise, if all pixels are below the threshold, the function returns `true`.
 *
 * @param mappedResource A reference to a `D3D11_MAPPED_SUBRESOURCE` structure that contains the mapped subresource data of the frame to be analyzed.
 * @param frameDesc A reference to a `D3D11_TEXTURE2D_DESC` structure that describes the texture properties, including width and height.
 * @param blackThreshold A floating-point value representing the threshold above which a pixel's RGB channels are considered non-black. The value ranges from 0.0f to 1.0f, with a default value of 0.1f.
 * @return bool Returns `true` if the frame is determined to be black, otherwise returns `false`.
 */
bool
isFrameBlack(const D3D11_MAPPED_SUBRESOURCE &mappedResource, const D3D11_TEXTURE2D_DESC &frameDesc, float blackThreshold = 0.1f) {
  const uint8_t *pixels = static_cast<const uint8_t *>(mappedResource.pData);
  const int bytesPerPixel = 4;  // Assuming RGBA format
  const int stride = mappedResource.RowPitch;
  const int width = frameDesc.Width;
  const int height = frameDesc.Height;

  // Convert the threshold to an integer value for comparison
  const int threshold = static_cast<int>(blackThreshold * 255);

  // Loop through every pixel
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint8_t *pixel = pixels + y * stride + x * bytesPerPixel;
      // Check if any channel (R, G, B) is significantly above black
      if (pixel[0] > threshold || pixel[1] > threshold || pixel[2] > threshold) {
        return false;
      }
    }
  }
  return true;
}

/**
 * @brief Attempts to capture and verify the contents of up to 10 consecutive frames from a DXGI output duplication.
 *
 * This function tries to acquire the next frame from a provided DXGI output duplication object (`dup`) and inspects its content to determine if the frame is not completely black. If a non-black frame is found within the 10 attempts, the function returns `S_OK`. If all 10 frames are black, it returns `S_FALSE`, indicating that the capture might not be functioning properly. In case of any failure during the process, the appropriate `HRESULT` error code is returned.
 *
 * @param dup A reference to the DXGI output duplication object (`dxgi::dup_t&`) used to acquire frames.
 * @param device A pointer to the ID3D11Device interface that represents the device associated with the Direct3D context.
 * @return HRESULT Returns `S_OK` if a non-black frame is captured successfully, `S_FALSE` if all frames are black, or an error code if a failure occurs during the process.
 *
 * Possible return values:
 * - `S_OK`: A non-black frame was captured, indicating capture was successful.
 * - `S_FALSE`: All 10 frames were black, indicating potential capture issues.
 * - `E_FAIL`, `DXGI_ERROR_*`, or other DirectX HRESULT error codes in case of specific failures during frame capture, texture creation, or resource mapping.
 */
HRESULT
test_frame_capture(dxgi::dup_t &dup, ID3D11Device *device) {
  for (int i = 0; i < 10; ++i) {
    std::cout << "Attempting to acquire the next frame (" << i + 1 << "/10)..." << std::endl;
    IDXGIResource *frameResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    HRESULT status = dup->AcquireNextFrame(500, &frameInfo, &frameResource);
    if (FAILED(status)) {
      std::cout << "Failed to acquire next frame [0x" << std::hex << status << "]" << std::endl;
      return status;
    }

    std::cout << "Frame acquired successfully." << std::endl;

    ID3D11Texture2D *frameTexture = nullptr;
    status = frameResource->QueryInterface(__uuidof(ID3D11Texture2D), (void **) &frameTexture);
    frameResource->Release();
    if (FAILED(status)) {
      std::cout << "Failed to query texture interface from frame resource [0x" << std::hex << status << "]" << std::endl;
      return status;
    }

    D3D11_TEXTURE2D_DESC frameDesc;
    frameTexture->GetDesc(&frameDesc);
    frameDesc.Usage = D3D11_USAGE_STAGING;
    frameDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    frameDesc.BindFlags = 0;
    frameDesc.MiscFlags = 0;

    ID3D11Texture2D *stagingTexture = nullptr;
    status = device->CreateTexture2D(&frameDesc, nullptr, &stagingTexture);
    if (FAILED(status)) {
      std::cout << "Failed to create staging texture [0x" << std::hex << status << "]" << std::endl;
      frameTexture->Release();
      return status;
    }

    ID3D11DeviceContext *context = nullptr;
    device->GetImmediateContext(&context);
    context->CopyResource(stagingTexture, frameTexture);
    frameTexture->Release();

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    status = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
    if (SUCCEEDED(status)) {
      std::cout << "Verifying if frame is not empty" << std::endl;

      if (!isFrameBlack(mappedResource, frameDesc)) {
        std::cout << "Frame " << i + 1 << " is not empty!" << std::endl;
        context->Unmap(stagingTexture, 0);
        stagingTexture->Release();
        context->Release();
        dup->ReleaseFrame();
        return S_OK;
      }
      context->Unmap(stagingTexture, 0);
    }
    else {
      std::cout << "Failed to map the staging texture for inspection [0x" << std::hex << status << "]" << std::endl;
      stagingTexture->Release();
      context->Release();
      return status;
    }

    stagingTexture->Release();
    context->Release();
    std::cout << "Releasing the frame..." << std::endl;
    dup->ReleaseFrame();
  }

  // If all frames are black, then we can assume the capture isn't working properly.
  return S_FALSE;
}

HRESULT
test_dxgi_duplication(dxgi::adapter_t &adapter, dxgi::output_t &output) {
  D3D_FEATURE_LEVEL featureLevels[] = {
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1
  };

  dxgi::device_t device;
  HRESULT status = D3D11CreateDevice(
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
    std::cout << "Failed to create D3D11 device for DD test [0x" << std::hex << status << "]" << std::endl;
    return status;
  }

  dxgi::output1_t output1;
  status = output->QueryInterface(IID_IDXGIOutput1, (void **) &output1);
  if (FAILED(status)) {
    std::cout << "Failed to query IDXGIOutput1 from the output [0x" << std::hex << status << "]" << std::endl;
    return status;
  }

  // Ensure we can duplicate the current display
  syncThreadDesktop();

  // Attempt to duplicate the output
  dxgi::dup_t dup;
  HRESULT result = output1->DuplicateOutput(static_cast<IUnknown *>(device.get()), &dup);

  if (SUCCEEDED(result)) {
    // If duplication is successful, test frame capture
    HRESULT captureResult = test_frame_capture(dup, device.get());
    if (SUCCEEDED(captureResult)) {
      return S_OK;
    }
    else {
      std::cout << "Frame capture test failed [0x" << std::hex << captureResult << "]" << std::endl;
      return captureResult;
    }
  }
  else {
    std::cout << "Failed to duplicate output [0x" << std::hex << result << "]" << std::endl;
    return result;
  }
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
