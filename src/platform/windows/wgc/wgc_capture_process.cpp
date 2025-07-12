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
#include <tlhelp32.h> // For process enumeration
#include <psapi.h> // For GetModuleBaseName
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


// Structure for config data received from main process
struct ConfigData {
    UINT width;
    UINT height;
    int framerate;
    int dynamicRange;
    wchar_t displayName[32]; // Display device name (e.g., "\\.\\DISPLAY1")
};

// Global config data received from main process
ConfigData g_config = {0, 0, 0, 0, L""};
bool g_config_received = false;

// Global communication pipe for sending session closed notifications
AsyncNamedPipe* g_communication_pipe = nullptr;

// Global variables for desktop switch detection
HWINEVENTHOOK g_desktop_switch_hook = nullptr;
bool g_secure_desktop_detected = false;

// Function to check if a process with the given name is running
bool IsProcessRunning(const std::wstring& processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W processEntry = {};
    processEntry.dwSize = sizeof(processEntry);

    bool found = false;
    if (Process32FirstW(snapshot, &processEntry)) {
        do {
            if (_wcsicmp(processEntry.szExeFile, processName.c_str()) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &processEntry));
    }

    CloseHandle(snapshot);
    return found;
}

// Function to check if we're on the secure desktop
bool IsSecureDesktop() {
    // Check for UAC (consent.exe)
    if (IsProcessRunning(L"consent.exe")) {
        return true;
    }

    // Check for login screen by looking for winlogon.exe with specific conditions
    // or check the current desktop name
    HDESK currentDesktop = GetThreadDesktop(GetCurrentThreadId());
    if (currentDesktop) {
        wchar_t desktopName[256] = {0};
        DWORD needed = 0;
        if (GetUserObjectInformationW(currentDesktop, UOI_NAME, desktopName, sizeof(desktopName), &needed)) {
            // Secure desktop typically has names like "Winlogon" or "SAD" (Secure Attention Desktop)
            if (_wcsicmp(desktopName, L"Winlogon") == 0 || _wcsicmp(desktopName, L"SAD") == 0) {
                return true;
            }
        }
    }

    return false;
}

// Desktop switch event hook procedure
void CALLBACK DesktopSwitchHookProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (event == EVENT_SYSTEM_DESKTOPSWITCH) {
        std::wcout << L"[WGC Helper] Desktop switch detected!" << std::endl;
        
        // Small delay to let the system settle
        Sleep(100);
        
        bool isSecure = IsSecureDesktop();
        std::wcout << L"[WGC Helper] Desktop switch - Secure desktop: " << (isSecure ? L"YES" : L"NO") << std::endl;
        
        if (isSecure && !g_secure_desktop_detected) {
            std::wcout << L"[WGC Helper] Secure desktop detected - sending notification to main process" << std::endl;
            g_secure_desktop_detected = true;
            
            // Send notification to main process
            if (g_communication_pipe && g_communication_pipe->isConnected()) {
                std::vector<uint8_t> sessionClosedMessage = {0x01}; // Simple marker for session closed
                g_communication_pipe->asyncSend(sessionClosedMessage);
                std::wcout << L"[WGC Helper] Sent secure desktop notification to main process" << std::endl;
            }
        } else if (!isSecure && g_secure_desktop_detected) {
            std::wcout << L"[WGC Helper] Returned to normal desktop" << std::endl;
            g_secure_desktop_detected = false;
        }
    }
}

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
    g_communication_pipe = &communicationPipe;  // Store global reference for session.Closed handler
    
    auto onMessage = [&](const std::vector<uint8_t>& message) {
        std::wcout << L"[WGC Helper] Received message from main process, size: " << message.size() << std::endl;
        // Handle config data message
        if (message.size() == sizeof(ConfigData) && !g_config_received) {
            memcpy(&g_config, message.data(), sizeof(ConfigData));
            g_config_received = true;
            std::wcout << L"[WGC Helper] Received config data: " << g_config.width << L"x" << g_config.height 
                       << L", fps: " << g_config.framerate << L", hdr: " << g_config.dynamicRange 
                       << L", display: '" << g_config.displayName << L"'" << std::endl;
        }
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


    // Select monitor by display name if provided, else use primary
    HMONITOR monitor = nullptr;
    MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
    if (g_config_received && g_config.displayName[0] != L'\0') {
        // Enumerate monitors to find one matching displayName
        struct EnumData {
            const wchar_t* targetName;
            HMONITOR foundMonitor;
        } enumData = { g_config.displayName, nullptr };
        auto enumProc = [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
            EnumData* data = reinterpret_cast<EnumData*>(lParam);
            MONITORINFOEXW info = { sizeof(MONITORINFOEXW) };
            if (GetMonitorInfoW(hMon, &info)) {
                if (wcsncmp(info.szDevice, data->targetName, 32) == 0) {
                    data->foundMonitor = hMon;
                    return FALSE; // Stop enumeration
                }
            }
            return TRUE;
        };
        EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&enumData));
        monitor = enumData.foundMonitor;
        if (!monitor) {
            std::wcerr << L"[WGC Helper] Could not find monitor with name '" << g_config.displayName << L"', falling back to primary." << std::endl;
        }
    }
    if (!monitor) {
        monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
        if (!monitor) {
            std::wcerr << L"[WGC Helper] Failed to get primary monitor" << std::endl;
            device->Release();
            context->Release();
            return 1;
        }
    }

    // Get monitor info for fallback size
    if (!GetMonitorInfo(monitor, &monitorInfo)) {
        std::wcerr << L"[WGC Helper] Failed to get monitor info" << std::endl;
        device->Release();
        context->Release();
        return 1;
    }
    UINT fallbackWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    UINT fallbackHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;

    // Check for config data from main process
    std::wcout << L"[WGC Helper] Checking for config data from main process..." << std::endl;
    int config_wait_count = 0;
    while (!g_config_received && config_wait_count < 50) {  // Increase to 5 seconds for reliability
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        config_wait_count++;
    }

    UINT width, height;
    if (g_config_received && g_config.width > 0 && g_config.height > 0) {
        width = g_config.width;
        height = g_config.height;
        std::wcout << L"[WGC Helper] Using config resolution: " << width << L"x" << height << std::endl;
    } else {
        width = fallbackWidth;
        height = fallbackHeight;
        std::wcout << L"[WGC Helper] No valid config resolution received, falling back to monitor: " << width << L"x" << height << std::endl;
    }



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

    // Choose format based on config.dynamicRange
    DXGI_FORMAT captureFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (g_config_received && g_config.dynamicRange) {
        captureFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    }

    // Create shared texture with keyed mutex (using config/fallback size)
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = captureFormat;
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

    // Create frame pool (using config/fallback size)
    auto framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        winrtDevice,
        (captureFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
            ? DirectXPixelFormat::R16G16B16A16Float
            : DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        SizeInt32{static_cast<int32_t>(width), static_cast<int32_t>(height)});

    // Attach frame arrived event
    auto token = framePool.FrameArrived([&](Direct3D11CaptureFramePool const &sender, winrt::Windows::Foundation::IInspectable const &)
                                        {
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
                        }
                    }
                }
            } catch (const winrt::hresult_error& ex) {
                std::wcerr << L"[WGC Helper] WinRT error in frame processing: " << ex.code() << L" - " << winrt::to_string(ex.message()).c_str() << std::endl;
            }
            surface.Close();
            frame.Close();
        } });

    // Set up desktop switch hook for secure desktop detection
    std::wcout << L"[WGC Helper] Setting up desktop switch hook..." << std::endl;
    g_desktop_switch_hook = SetWinEventHook(
        EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH,
        nullptr, DesktopSwitchHookProc,
        0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );
    
    if (!g_desktop_switch_hook) {
        std::wcerr << L"[WGC Helper] Failed to set up desktop switch hook: " << GetLastError() << std::endl;
    } else {
        std::wcout << L"[WGC Helper] Desktop switch hook installed successfully" << std::endl;
    }

    // Start capture
    auto session = framePool.CreateCaptureSession(item);
    // Disable border/highlight
    session.IsBorderRequired(false);
    session.StartCapture();

    std::wcout << L"[WGC Helper] Helper process started. Capturing frames using WGC..." << std::endl;

    // Keep running until main process disconnects
    // We need to pump messages for the desktop switch hook to work
    MSG msg;
    while (communicationPipe.isConnected())
    {
        // Process any pending messages for the hook
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::wcout << L"[WGC Helper] Main process disconnected, shutting down..." << std::endl;

    // Cleanup
    if (g_desktop_switch_hook) {
        UnhookWinEvent(g_desktop_switch_hook);
        g_desktop_switch_hook = nullptr;
    }
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