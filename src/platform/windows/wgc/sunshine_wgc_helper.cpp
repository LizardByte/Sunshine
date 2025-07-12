// sunshine_wgc_helper.cpp
// Windows Graphics Capture helper process for Sunshine

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <winrt/base.h>
#include <winrt/windows.foundation.h>
#include <winrt/windows.foundation.metadata.h>
#include <winrt/windows.graphics.directx.direct3d11.h>
#include <winrt/Windows.System.h>
#include <inspectable.h> // For IInspectable
#include "shared_memory.h"
// Gross hack to work around MINGW-packages#22160
#define ____FIReference_1_boolean_INTERFACE_DEFINED__

// Manual declaration for CreateDirect3D11DeviceFromDXGIDevice if missing
extern "C"
{
    HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(::IDXGIDevice *dxgiDevice, ::IInspectable **graphicsDevice);
}

/**
 * Windows structures sometimes have compile-time GUIDs. GCC supports this, but in a roundabout way.
 * If WINRT_IMPL_HAS_DECLSPEC_UUID is true, then the compiler supports adding this attribute to a struct. For example, Visual Studio.
 * If not, then MinGW GCC has a workaround to assign a GUID to a structure.
 */
struct
#if WINRT_IMPL_HAS_DECLSPEC_UUID
    __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
#endif
    IDirect3DDxgiInterfaceAccess : ::IUnknown
{
    virtual HRESULT __stdcall GetInterface(REFIID id, void **object) = 0;
};


#if !WINRT_IMPL_HAS_DECLSPEC_UUID
static constexpr GUID GUID__IDirect3DDxgiInterfaceAccess = {
    0xA9B3D012,
    0x3DF2,
    0x4EE3,
    {0xB8, 0xD1, 0x86, 0x95, 0xF4, 0x57, 0xD3, 0xC1}
};
template <>
constexpr auto __mingw_uuidof<IDirect3DDxgiInterfaceAccess>() -> GUID const &
{
    return GUID__IDirect3DDxgiInterfaceAccess;
}

static constexpr GUID GUID__IDirect3DSurface = {
    0x0BF4A146,
    0x13C1,
    0x4694,
    {0xBE, 0xE3, 0x7A, 0xBF, 0x15, 0xEA, 0xF5, 0x86}
};
template <>
constexpr auto __mingw_uuidof<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface>() -> GUID const &
{
    return GUID__IDirect3DSurface;
}
#endif

#include <windows.graphics.capture.interop.h>
#include <winrt/windows.graphics.capture.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::System;

std::mutex contextMutex;

// Structure for shared handle data sent via named pipe
struct SharedHandleData {
    HANDLE textureHandle;
    UINT width;
    UINT height;
};


#include <fstream>

int main()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // Redirect wcout and wcerr to a file
    std::wofstream logFile(L"sunshine_wgc_helper.log", std::ios::out | std::ios::trunc);
    if (logFile.is_open()) {
        std::wcout.rdbuf(logFile.rdbuf());
        std::wcerr.rdbuf(logFile.rdbuf());
    } else {
        // If log file can't be opened, print error to default wcerr
        std::wcerr << L"[WGC Helper] Failed to open log file for output!" << std::endl;
    }

    std::wcout << L"[WGC Helper] Starting Windows Graphics Capture helper process..." << std::endl;

    // Create named pipe for communication with main process
    AsyncNamedPipe communicationPipe(L"\\\\.\\pipe\\SunshineWGCHelper", true);
    
    auto onMessage = [&](const std::vector<uint8_t>& message) {
        std::wcout << L"[WGC Helper] Received message from main process, size: " << message.size() << std::endl;
        // Handle any commands from main process if needed
    };
    
    auto onError = [&](const std::string& error) {
        std::wcout << L"[WGC Helper] Pipe error: " << error.c_str() << std::endl;
    };
    
    if (!communicationPipe.start(onMessage, onError)) {
        std::wcerr << L"[WGC Helper] Failed to start communication pipe" << std::endl;
        return 1;
    }

    // Create D3D11 device and context
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &featureLevel, &context);
    if (FAILED(hr))
    {
        std::wcerr << L"[WGC Helper] Failed to create D3D11 device" << std::endl;
        return 1;
    }

    // Wrap D3D11 device to WinRT IDirect3DDevice
    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    hr = device->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void());
    if (FAILED(hr))
    {
        std::wcerr << L"[WGC Helper] Failed to get DXGI device" << std::endl;
        device->Release();
        context->Release();
        return 1;
    }
    winrt::com_ptr<::IDirect3DDevice> interopDevice;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), reinterpret_cast<::IInspectable **>(interopDevice.put_void()));
    if (FAILED(hr))
    {
        std::wcerr << L"[WGC Helper] Failed to create interop device" << std::endl;
        device->Release();
        context->Release();
        return 1;
    }
    auto winrtDevice = interopDevice.as<IDirect3DDevice>();

    // Get primary monitor
    HMONITOR monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    if (!monitor) {
        std::wcerr << L"[WGC Helper] Failed to get primary monitor" << std::endl;
        device->Release();
        context->Release();
        return 1;
    }

    // Get monitor info for size
    MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
    if (!GetMonitorInfo(monitor, &monitorInfo)) {
        std::wcerr << L"[WGC Helper] Failed to get monitor info" << std::endl;
        device->Release();
        context->Release();
        return 1;
    }
    UINT width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    UINT height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;

    // Create GraphicsCaptureItem for monitor using interop
    auto activationFactory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item = nullptr;
    hr = activationFactory->CreateForMonitor(monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item));
    if (FAILED(hr)) {
        std::wcerr << L"[WGC Helper] Failed to create GraphicsCaptureItem for monitor: " << hr << std::endl;
        device->Release();
        context->Release();
        return 1;
    }

    // Create shared texture with keyed mutex
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = 0;
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ID3D11Texture2D *sharedTexture = nullptr;
    hr = device->CreateTexture2D(&texDesc, nullptr, &sharedTexture);
    if (FAILED(hr))
    {
        std::wcerr << L"[WGC Helper] Failed to create shared texture" << std::endl;
        device->Release();
        context->Release();
        return 1;
    }

    // Get keyed mutex
    IDXGIKeyedMutex *keyedMutex = nullptr;
    hr = sharedTexture->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&keyedMutex));
    if (FAILED(hr))
    {
        std::wcerr << L"[WGC Helper] Failed to get keyed mutex" << std::endl;
        sharedTexture->Release();
        device->Release();
        context->Release();
        return 1;
    }

    // Get shared handle
    IDXGIResource *dxgiResource = nullptr;
    hr = sharedTexture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&dxgiResource));
    HANDLE sharedHandle = nullptr;
    hr = dxgiResource->GetSharedHandle(&sharedHandle);
    dxgiResource->Release();
    if (FAILED(hr) || !sharedHandle)
    {
        std::wcerr << L"[WGC Helper] Failed to get shared handle" << std::endl;
        keyedMutex->Release();
        sharedTexture->Release();
        device->Release();
        context->Release();
        return 1;
    }

    std::wcout << L"[WGC Helper] Created shared texture: " << width << L"x" << height 
               << L", handle: " << std::hex << reinterpret_cast<uintptr_t>(sharedHandle) << std::dec << std::endl;

    // Send shared handle data via named pipe to main process
    SharedHandleData handleData = { sharedHandle, width, height };
    std::vector<uint8_t> handleMessage(sizeof(SharedHandleData));
    memcpy(handleMessage.data(), &handleData, sizeof(SharedHandleData));
    
    // Wait for connection and send the handle data
    std::wcout << L"[WGC Helper] Waiting for main process to connect..." << std::endl;
    while (!communicationPipe.isConnected()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::wcout << L"[WGC Helper] Connected! Sending handle data..." << std::endl;
    communicationPipe.asyncSend(handleMessage);

    // Create event to signal new frame
    HANDLE frameEvent = CreateEventW(nullptr, FALSE, FALSE, L"Local\\SunshineWGCFrame");
    if (!frameEvent)
    {
        std::wcerr << L"[WGC Helper] Failed to create frame event" << std::endl;
        keyedMutex->Release();
        sharedTexture->Release();
        device->Release();
        context->Release();
        return 1;
    }

    // Create frame pool
    auto framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, SizeInt32{static_cast<int32_t>(width), static_cast<int32_t>(height)});

    // Attach frame arrived event
    auto token = framePool.FrameArrived([&](Direct3D11CaptureFramePool const &sender, winrt::Windows::Foundation::IInspectable const &)
                                        {
        std::wcout << L"[WGC Helper] Frame arrived" << std::endl;
        auto frame = sender.TryGetNextFrame();
        if (frame) {
            auto surface = frame.Surface();
            try {
                winrt::com_ptr<IDirect3DDxgiInterfaceAccess> interfaceAccess;
                hr = surface.as<::IUnknown>()->QueryInterface(__uuidof(IDirect3DDxgiInterfaceAccess), reinterpret_cast<void**>(interfaceAccess.put()));
                if (FAILED(hr)) {
                    std::wcerr << L"[WGC Helper] Failed to query IDirect3DDxgiInterfaceAccess: " << hr << std::endl;
                } else {
                    winrt::com_ptr<ID3D11Texture2D> frameTexture;
                    hr = interfaceAccess->GetInterface(__uuidof(ID3D11Texture2D), frameTexture.put_void());
                    if (FAILED(hr)) {
                        std::wcerr << L"[WGC Helper] Failed to get ID3D11Texture2D from interface: " << hr << std::endl;
                    } else {
                        std::lock_guard<std::mutex> lock(contextMutex);
                        hr = keyedMutex->AcquireSync(0, INFINITE);
                        if (FAILED(hr)) {
                            std::wcerr << L"[WGC Helper] Failed to acquire keyed mutex: " << hr << std::endl;
                        } else {
                            context->CopyResource(sharedTexture, frameTexture.get());
                            keyedMutex->ReleaseSync(1);
                            SetEvent(frameEvent);
                            std::wcout << L"[WGC Helper] Frame copied and event set" << std::endl;
                        }
                    }
                }
            } catch (const winrt::hresult_error& ex) {
                std::wcerr << L"[WGC Helper] WinRT error in frame processing: " << ex.code() << L" - " << winrt::to_string(ex.message()).c_str() << std::endl;
            }
            surface.Close();
            frame.Close();
        } });

    // Start capture
    auto session = framePool.CreateCaptureSession(item);
    session.StartCapture();

    std::wcout << L"[WGC Helper] Helper process started. Capturing frames using WGC..." << std::endl;

    // Keep running until main process disconnects
    while (communicationPipe.isConnected())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    std::wcout << L"[WGC Helper] Main process disconnected, shutting down..." << std::endl;

    // Cleanup
    session.Close();
    framePool.FrameArrived(token);
    framePool.Close();
    CloseHandle(frameEvent);
    communicationPipe.stop();
    keyedMutex->Release();
    sharedTexture->Release();
    context->Release();
    device->Release();
    // logFile will be closed automatically when it goes out of scope
    return 0;
}