#include "antihookingprotection.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <detours.h>

typedef HMODULE (WINAPI *LoadLibraryAFunc)(LPCSTR lpLibFileName);
typedef HMODULE (WINAPI *LoadLibraryWFunc)(LPCWSTR lpLibFileName);
typedef HMODULE (WINAPI *LoadLibraryExAFunc)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
typedef HMODULE (WINAPI *LoadLibraryExWFunc)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

class AntiHookingProtection
{
public:
    static void enable()
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        s_RealLoadLibraryA = LoadLibraryA;
        DetourAttach(&(PVOID&)s_RealLoadLibraryA, LoadLibraryAHook);

        s_RealLoadLibraryW = LoadLibraryW;
        DetourAttach(&(PVOID&)s_RealLoadLibraryW, LoadLibraryWHook);

        s_RealLoadLibraryExA = LoadLibraryExA;
        DetourAttach(&(PVOID&)s_RealLoadLibraryExA, LoadLibraryExAHook);

        s_RealLoadLibraryExW = LoadLibraryExW;
        DetourAttach(&(PVOID&)s_RealLoadLibraryExW, LoadLibraryExWHook);

        DetourTransactionCommit();
    }

    static void disable()
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourDetach(&(PVOID&)s_RealLoadLibraryA, LoadLibraryAHook);
        DetourDetach(&(PVOID&)s_RealLoadLibraryW, LoadLibraryWHook);
        DetourDetach(&(PVOID&)s_RealLoadLibraryExA, LoadLibraryExAHook);
        DetourDetach(&(PVOID&)s_RealLoadLibraryExW, LoadLibraryExWHook);

        DetourTransactionCommit();
    }

private:
    static bool isImageBlacklistedW(LPCWSTR lpLibFileName)
    {
        LPCWSTR dllName;

        // If the library has a path prefixed, remove it
        dllName = wcsrchr(lpLibFileName, '\\');
        if (!dllName) {
            // No prefix, so use the full name
            dllName = lpLibFileName;
        }
        else {
            // Advance past the backslash
            dllName++;
        }

        // FIXME: We don't currently handle LoadLibrary calls where the
        // library name does not include a file extension and the loader
        // automatically assumes .dll.

        for (int i = 0; i < ARRAYSIZE(k_BlacklistedDlls); i++) {
            if (_wcsicmp(dllName, k_BlacklistedDlls[i]) == 0) {
                return true;
            }
        }

        return false;
    }

    static bool isImageBlacklistedA(LPCSTR lpLibFileName)
    {
        int uniChars = MultiByteToWideChar(CP_THREAD_ACP, 0, lpLibFileName, -1, nullptr, 0);
        if (uniChars > 0) {
            PWCHAR wideBuffer = new WCHAR[uniChars];
            uniChars = MultiByteToWideChar(CP_THREAD_ACP, 0,
                                           lpLibFileName, -1,
                                           wideBuffer, uniChars * sizeof(WCHAR));
            if (uniChars > 0) {
                bool ret = isImageBlacklistedW(wideBuffer);
                delete[] wideBuffer;
                return ret;
            }
            else {
                delete[] wideBuffer;
            }
        }

        // Error path
        return false;
    }

    static HMODULE WINAPI LoadLibraryAHook(LPCSTR lpLibFileName)
    {
        if (lpLibFileName && isImageBlacklistedA(lpLibFileName)) {
            SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY);
            return nullptr;
        }

        return s_RealLoadLibraryA(lpLibFileName);
    }

    static HMODULE WINAPI LoadLibraryWHook(LPCWSTR lpLibFileName)
    {
        if (lpLibFileName && isImageBlacklistedW(lpLibFileName)) {
            SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY);
            return nullptr;
        }

        return s_RealLoadLibraryW(lpLibFileName);
    }

    static HMODULE WINAPI LoadLibraryExAHook(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        if (lpLibFileName && isImageBlacklistedA(lpLibFileName)) {
            SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY);
            return nullptr;
        }

        return s_RealLoadLibraryExA(lpLibFileName, hFile, dwFlags);
    }

    static HMODULE WINAPI LoadLibraryExWHook(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        if (lpLibFileName && isImageBlacklistedW(lpLibFileName)) {
            SetLastError(ERROR_ACCESS_DISABLED_BY_POLICY);
            return nullptr;
        }

        return s_RealLoadLibraryExW(lpLibFileName, hFile, dwFlags);
    }

    static LoadLibraryAFunc s_RealLoadLibraryA;
    static LoadLibraryWFunc s_RealLoadLibraryW;
    static LoadLibraryExAFunc s_RealLoadLibraryExA;
    static LoadLibraryExWFunc s_RealLoadLibraryExW;

    static constexpr LPCWSTR k_BlacklistedDlls[] = {
        // These A-Volute DLLs shipped with various audio driver packages improperly handle
        // D3D9 exclusive fullscreen in a way that causes CreateDeviceEx() to deadlock.
        // https://github.com/moonlight-stream/moonlight-qt/issues/102
        L"NahimicOSD.dll", // ASUS Sonic Radar 3
        L"SSAudioOSD.dll", // SteelSeries headsets
        L"SS2OSD.dll", // ASUS Sonic Studio 2
        L"Nahimic2OSD.dll",
        L"NahimicMSIOSD.dll",
        L"nhAsusPhoebusOSD.dll", // ASUS Phoebus

        // This DLL has been seen in several crash reports. Some Googling
        // suggests it's highly unstable and causes issues in many games.
        L"EZFRD32.dll",
        L"EZFRD64.dll",

        // These are the older dList/AppInit DLLs for Optimus hybrid graphics DDI.
        // https://docs.microsoft.com/en-us/windows-hardware/drivers/display/hybrid-system-ddi
        //
        // These seem to cause a crash in PresentEx() in full-screen exclusive mode.
        // This block will prevent Optimus from ever using the dGPU even if the user has requested it.
        // https://github.com/moonlight-stream/moonlight-qt/issues/386
        //
        // d3d9!CSwapChain::BltToHybridPrimary+0x200:
        // 00007ffa`23f37e58 488b01          mov     rax,qword ptr [rcx] ds:00000000`00000038=????????????????
        // 00 0000004e`496ff4e0 00007ffa`23f39e2c d3d9!CSwapChain::BltToHybridPrimary+0x200
        // 01 0000004e`496ff880 00007ffa`23ee39ce d3d9!CSwapChain::FlipToSurface+0x15c
        // 02 0000004e`496ff900 00007ffa`23f4dd75 d3d9!CSwapChain::PresentMain+0x3e13e
        // 03 0000004e`496ffab0 00007ffa`23f4dccd d3d9!CBaseDevice::PresentMain+0x9d
        // 04 0000004e`496ffb00 00007ff7`8e31016f d3d9!CBaseDevice::PresentEx+0xbd
        // 05 0000004e`496ffb50 00007ff7`8e30df1e Moonlight!DXVA2Renderer::renderFrame+0x61f [C:\moonlight-qt\app\streaming\video\ffmpeg-renderers\dxva2.cpp @ 1035]
        // 06 0000004e`496ffd50 00007ff7`8e30e46a Moonlight!Pacer::renderFrame+0x3e [C:\moonlight-qt\app\streaming\video\ffmpeg-renderers\pacer\pacer.cpp @ 265]
        // 07 (Inline Function) --------`-------- Moonlight!Pacer::renderLastFrameAndUnlock+0x197 [C:\moonlight-qt\app\streaming\video\ffmpeg-renderers\pacer\pacer.cpp @ 156]
        // 08 0000004e`496ffda0 00007ffa`14476978 Moonlight!Pacer::renderThread+0x1ca [C:\moonlight-qt\app\streaming\video\ffmpeg-renderers\pacer\pacer.cpp @ 88]
        // 09 0000004e`496ffde0 00007ffa`14476ee2 SDL2!SDL_RunThread+0x38 [C:\Users\camer\SDL\src\thread\SDL_thread.c @ 276]
        // 0a 0000004e`496ffe10 00007ffa`2aae0e82 SDL2!RunThread+0x12 [C:\Users\camer\SDL\src\thread\windows\SDL_systhread.c @ 83]
        // 0b 0000004e`496ffe40 00007ffa`2d627bd4 ucrtbase!thread_start<unsigned int (__cdecl*)(void *),1>+0x42
        // 0c 0000004e`496ffe70 00007ffa`2da4ce51 kernel32!BaseThreadInitThunk+0x14
        // 0d 0000004e`496ffea0 00000000`00000000 ntdll!RtlUserThreadStart+0x21
        //
        L"nvinit.dll",
        L"nvinitx.dll",

        // In some unknown circumstances, RTSS tries to hook in the middle of an instruction, leaving garbage
        // code inside d3d9.dll that causes a crash when executed:
        //
        // 0:000> u
        // d3d9!D3D9GetCurrentOwnershipMode+0x5d:
        // 00007ff8`95b95861 9b              wait
        // 00007ff8`95b95862 a7              cmps    dword ptr [rsi],dword ptr [rdi]   <--- crash happens here
        // 00007ff8`95b95863 ff              ???
        // 00007ff8`95b95864 bfe8ca8a00      mov     edi,8ACAE8h
        // 00007ff8`95b95869 00eb            add     bl,ch
        // 00007ff8`95b9586b f1              ???
        // 00007ff8`95b9586c b808000000      mov     eax,8
        // 00007ff8`95b95871 ebe6            jmp     d3d9!D3D9GetCurrentOwnershipMode+0x55 (00007ff8`95b95859)
        //
        // Disassembling starting at the exact address of the attempted hook yields the intended jmp instruction
        //
        // 0:000> u d3d9!D3D9GetCurrentOwnershipMode+0x5c:
        // 00007ff8`95b95860 e99ba7ffbf      jmp     00007ff8`55b90000
        //
        // Since the RTSS OSD doesn't even work with DXVA2, we'll just block the hooks entirely.
        L"RTSSHooks.dll",
        L"RTSSHooks64.dll",

        // Bandicam's Vulkan layer DLL crashes during destruction of the Vulkan swapchain
        // bdcamvk64+0x10b73:
        // 00007ffa`be3b0b73 498b5630        mov     rdx,qword ptr [r14+30h] ds:00000000`00000030=????????????????
        //
        // bdcamvk64+0x10b73
        // libplacebo_357!vk_sw_destroy+0x14f [D:\a\moonlight-deps\moonlight-deps\libplacebo\src\vulkan\swapchain.c @ 460]
        // libplacebo_357!pl_swapchain_destroy+0x1a [D:\a\moonlight-deps\moonlight-deps\libplacebo\src\swapchain.c @ 30]
        // Moonlight!PlVkRenderer::~PlVkRenderer+0x159 [D:\a\moonlight-qt\app\streaming\video\ffmpeg-renderers\plvk.cpp @ 140]
        // Moonlight!PlVkRenderer::`scalar deleting destructor'+0x17
        // Moonlight!FFmpegVideoDecoder::reset+0x203 [D:\a\moonlight-qt\app\streaming\video\ffmpeg.cpp @ 301]
        // Moonlight!FFmpegVideoDecoder::~FFmpegVideoDecoder+0x22 [D:\a\moonlight-qt\app\streaming\video\ffmpeg.cpp @ 257]
        // Moonlight!FFmpegVideoDecoder::`scalar deleting destructor'+0x17
        // Moonlight!Session::getDecoderInfo+0x19a [D:\a\moonlight-qt\app\streaming\session.cpp @ 415]
        // Moonlight!SystemProperties::querySdlVideoInfoInternal+0x134 [D:\a\moonlight-qt\app\backend\systemproperties.cpp @ 161]
        // Moonlight!SystemProperties::querySdlVideoInfo+0x97 [D:\a\moonlight-qt\app\backend\systemproperties.cpp @ 123]
        // Moonlight!SystemProperties::SystemProperties+0x4bf [D:\a\moonlight-qt\app\backend\systemproperties.cpp @ 75]
        //
        // https://github.com/moonlight-stream/moonlight-qt/issues/1425
        // https://github.com/moonlight-stream/moonlight-qt/issues/1579
        L"bdcamvk32.dll",
        L"bdcamvk64.dll",
    };
};

LoadLibraryAFunc AntiHookingProtection::s_RealLoadLibraryA;
LoadLibraryWFunc AntiHookingProtection::s_RealLoadLibraryW;
LoadLibraryExAFunc AntiHookingProtection::s_RealLoadLibraryExA;
LoadLibraryExWFunc AntiHookingProtection::s_RealLoadLibraryExW;

AH_EXPORT void AntiHookingDummyImport() {}

extern "C"
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        DetourRestoreAfterWith();
        AntiHookingProtection::enable();
        DisableThreadLibraryCalls(hinstDLL);
        break;
    case DLL_PROCESS_DETACH:
        // Ignore DLL_PROCESS_DETACH on process exit. No need to waste time
        // unhooking everything if the whole process is being destroyed.
        if (lpvReserved == NULL) {
            AntiHookingProtection::disable();
        }
        break;
    }

    return TRUE;
};
