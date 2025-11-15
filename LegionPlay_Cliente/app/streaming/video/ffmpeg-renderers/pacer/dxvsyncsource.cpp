#include "dxvsyncsource.h"

// Useful references:
// https://bugs.chromium.org/p/chromium/issues/detail?id=467617
// https://chromium.googlesource.com/chromium/src.git/+/c564f2fe339b2b2abb0c8773c90c83215670ea71/gpu/ipc/service/gpu_vsync_provider_win.cc

DxVsyncSource::DxVsyncSource(Pacer* pacer) :
    m_Pacer(pacer),
    m_Gdi32Handle(nullptr),
    m_LastMonitor(nullptr)
{
    SDL_zero(m_WaitForVblankEventParams);
}

DxVsyncSource::~DxVsyncSource()
{
    if (m_WaitForVblankEventParams.hAdapter != 0) {
        D3DKMT_CLOSEADAPTER closeAdapterParams = {};
        closeAdapterParams.hAdapter = m_WaitForVblankEventParams.hAdapter;
        m_D3DKMTCloseAdapter(&closeAdapterParams);
    }

    if (m_Gdi32Handle != nullptr) {
        FreeLibrary(m_Gdi32Handle);
    }
}

bool DxVsyncSource::initialize(SDL_Window* window, int)
{
    m_Gdi32Handle = LoadLibraryA("gdi32.dll");
    if (m_Gdi32Handle == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to load gdi32.dll: %d",
                     GetLastError());
        return false;
    }

    m_D3DKMTOpenAdapterFromHdc = (PFND3DKMTOPENADAPTERFROMHDC)GetProcAddress(m_Gdi32Handle, "D3DKMTOpenAdapterFromHdc");
    m_D3DKMTCloseAdapter = (PFND3DKMTCLOSEADAPTER)GetProcAddress(m_Gdi32Handle, "D3DKMTCloseAdapter");
    m_D3DKMTWaitForVerticalBlankEvent = (PFND3DKMTWAITFORVERTICALBLANKEVENT)GetProcAddress(m_Gdi32Handle, "D3DKMTWaitForVerticalBlankEvent");

    if (m_D3DKMTOpenAdapterFromHdc == nullptr ||
            m_D3DKMTCloseAdapter == nullptr ||
            m_D3DKMTWaitForVerticalBlankEvent == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Missing required function in gdi32.dll");
        return false;
    }

    SDL_SysWMinfo info;

    SDL_VERSION(&info.version);

    if (!SDL_GetWindowWMInfo(window, &info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_GetWindowWMInfo() failed: %s",
                     SDL_GetError());
        return false;
    }

    // Pacer should only create us on Win32
    SDL_assert(info.subsystem == SDL_SYSWM_WINDOWS);

    m_Window = info.info.win.window;

    return true;
}

bool DxVsyncSource::isAsync()
{
    // We wait in the context of the Pacer thread
    return false;
}

void DxVsyncSource::waitForVsync()
{
    NTSTATUS status;

    // If the monitor has changed from last time, open the new adapter
    HMONITOR currentMonitor = MonitorFromWindow(m_Window, MONITOR_DEFAULTTONEAREST);
    if (currentMonitor != m_LastMonitor) {
        MONITORINFOEXA monitorInfo = {};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!GetMonitorInfoA(currentMonitor, &monitorInfo)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "GetMonitorInfo() failed: %d",
                         GetLastError());
            return;
        }

        DEVMODEA monitorMode;
        monitorMode.dmSize = sizeof(monitorMode);
        if (!EnumDisplaySettingsA(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &monitorMode)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "EnumDisplaySettings() failed: %d",
                         GetLastError());
            return;
        }

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Monitor changed: %s %d Hz",
                    monitorInfo.szDevice,
                    monitorMode.dmDisplayFrequency);

        // Close the old adapter
        if (m_WaitForVblankEventParams.hAdapter != 0) {
            D3DKMT_CLOSEADAPTER closeAdapterParams = {};
            closeAdapterParams.hAdapter = m_WaitForVblankEventParams.hAdapter;
            m_D3DKMTCloseAdapter(&closeAdapterParams);
        }

        D3DKMT_OPENADAPTERFROMHDC openAdapterParams = {};
        openAdapterParams.hDc = CreateDCA(nullptr, monitorInfo.szDevice, nullptr, nullptr);
        if (!openAdapterParams.hDc) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "CreateDC() failed: %d",
                         GetLastError());
            return;
        }

        // Open the new adapter
        status = m_D3DKMTOpenAdapterFromHdc(&openAdapterParams);
        DeleteDC(openAdapterParams.hDc);

        if (status != STATUS_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "D3DKMTOpenAdapterFromHdc() failed: %x",
                         status);
            return;
        }

        m_WaitForVblankEventParams.hAdapter = openAdapterParams.hAdapter;
        m_WaitForVblankEventParams.hDevice = 0;
        m_WaitForVblankEventParams.VidPnSourceId = openAdapterParams.VidPnSourceId;

        m_LastMonitor = currentMonitor;
    }

    status = m_D3DKMTWaitForVerticalBlankEvent(&m_WaitForVblankEventParams);
    if (status != STATUS_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3DKMTWaitForVerticalBlankEvent() failed: %x",
                     status);
        return;
    }
}
