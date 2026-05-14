/*
 * Copyright (c) 2023, Nguyen Duy <wuuyi123@gmail.com> All rights reserved.
 * GitHub repo: https://github.com/nomi-san/parsec-vdd/
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __PARSEC_VDD_H
#define __PARSEC_VDD_H

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#ifdef _MSC_VER
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "setupapi.lib")
#endif

#ifdef __cplusplus
namespace parsec_vdd
{
#endif

// Device helper.
//////////////////////////////////////////////////

typedef enum {
    DEVICE_OK = 0,             // Ready to use
    DEVICE_INACCESSIBLE,       // Inaccessible
    DEVICE_UNKNOWN,            // Unknown status
    DEVICE_UNKNOWN_PROBLEM,    // Unknown problem
    DEVICE_DISABLED,           // Device is disabled
    DEVICE_DRIVER_ERROR,       // Device encountered error
    DEVICE_RESTART_REQUIRED,   // Must restart PC to use (could ignore but would have issue)
    DEVICE_DISABLED_SERVICE,   // Service is disabled
    DEVICE_NOT_INSTALLED       // Driver is not installed
} DeviceStatus;

/**
* Query the driver status.
*
* @param classGuid The GUID of the class.
* @param deviceId The device/hardware ID of the driver.
* @return DeviceStatus
*/
static DeviceStatus QueryDeviceStatus(const GUID *classGuid, const char *deviceId)
{
    DeviceStatus status = DEVICE_INACCESSIBLE;

    SP_DEVINFO_DATA devInfoData;
    ZeroMemory(&devInfoData, sizeof(SP_DEVINFO_DATA));
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    HDEVINFO devInfo = SetupDiGetClassDevsA(classGuid, NULL, NULL, DIGCF_PRESENT);

    if (devInfo != INVALID_HANDLE_VALUE)
    {
        BOOL foundProp = FALSE;
        UINT deviceIndex = 0;

        do
        {
            if (!SetupDiEnumDeviceInfo(devInfo, deviceIndex, &devInfoData))
                break;

            DWORD requiredSize = 0;
            SetupDiGetDeviceRegistryPropertyA(devInfo, &devInfoData,
                SPDRP_HARDWAREID, NULL, NULL, 0, &requiredSize);

            if (requiredSize > 0)
            {
                DWORD regDataType = 0;
                LPBYTE propBuffer = (LPBYTE)calloc(1, requiredSize);

                if (SetupDiGetDeviceRegistryPropertyA(
                    devInfo,
                    &devInfoData,
                    SPDRP_HARDWAREID,
                    &regDataType,
                    propBuffer,
                    requiredSize,
                    &requiredSize))
                {
                    if (regDataType == REG_SZ || regDataType == REG_MULTI_SZ)
                    {
                        for (LPCSTR cp = (LPCSTR)propBuffer; ; cp += lstrlenA(cp) + 1)
                        {
                            if (!cp || *cp == 0 || cp >= (LPCSTR)(propBuffer + requiredSize))
                            {
                                status = DEVICE_NOT_INSTALLED;
                                goto except;
                            }

                            if (lstrcmpA(deviceId, cp) == 0)
                                break;
                        }

                        foundProp = TRUE;
                        ULONG devStatus, devProblemNum;

                        if (CM_Get_DevNode_Status(&devStatus, &devProblemNum, devInfoData.DevInst, 0) != CR_SUCCESS)
                        {
                            status = DEVICE_NOT_INSTALLED;
                            goto except;
                        }

                        if ((devStatus & (DN_DRIVER_LOADED | DN_STARTED)) != 0)
                        {
                            status = DEVICE_OK;
                        }
                        else if ((devStatus & DN_HAS_PROBLEM) != 0)
                        {
                            switch (devProblemNum)
                            {
                            case CM_PROB_NEED_RESTART:
                                status = DEVICE_RESTART_REQUIRED;
                                break;
                            case CM_PROB_DISABLED:
                            case CM_PROB_HARDWARE_DISABLED:
                                status = DEVICE_DISABLED;
                                break;
                            case CM_PROB_DISABLED_SERVICE:
                                status = DEVICE_DISABLED_SERVICE;
                                break;
                            default:
                                if (devProblemNum == CM_PROB_FAILED_POST_START)
                                    status = DEVICE_DRIVER_ERROR;
                                else
                                    status = DEVICE_UNKNOWN_PROBLEM;
                                break;
                            }
                        }
                        else
                        {
                            status = DEVICE_UNKNOWN;
                        }
                    }
                }

            except:
                free(propBuffer);
            }

            ++deviceIndex;
        } while (!foundProp);

        if (!foundProp && GetLastError() != 0)
            status = DEVICE_NOT_INSTALLED;

        SetupDiDestroyDeviceInfoList(devInfo);
    }

    return status;
}

/**
* Obtain the device handle.
* Returns NULL or INVALID_HANDLE_VALUE if fails, otherwise a valid handle.
* Should call CloseDeviceHandle to close this handle after use.
*
* @param interfaceGuid The adapter/interface GUID of the target device.
* @return HANDLE
*/
static HANDLE OpenDeviceHandle(const GUID *interfaceGuid)
{
    HANDLE handle = INVALID_HANDLE_VALUE;
    HDEVINFO devInfo = SetupDiGetClassDevsA(interfaceGuid,
        NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (devInfo != INVALID_HANDLE_VALUE)
    {
        SP_DEVICE_INTERFACE_DATA devInterface;
        ZeroMemory(&devInterface, sizeof(SP_DEVICE_INTERFACE_DATA));
        devInterface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, interfaceGuid, i, &devInterface); ++i)
        {
            DWORD detailSize = 0;
            SetupDiGetDeviceInterfaceDetailA(devInfo, &devInterface, NULL, 0, &detailSize, NULL);

            SP_DEVICE_INTERFACE_DETAIL_DATA_A *detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *)calloc(1, detailSize);
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

            if (SetupDiGetDeviceInterfaceDetailA(devInfo, &devInterface, detail, detailSize, &detailSize, NULL))
            {
                handle = CreateFileA(detail->DevicePath,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED | FILE_FLAG_WRITE_THROUGH,
                    NULL);

                if (handle != NULL && handle != INVALID_HANDLE_VALUE)
                    break;
            }

            free(detail);
        }

        SetupDiDestroyDeviceInfoList(devInfo);
    }

    return handle;
}

/* Release the device handle */
static void CloseDeviceHandle(HANDLE handle)
{
    if (handle != NULL && handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
}

// Parsec VDD core.
//////////////////////////////////////////////////

// Display name info.
static const char *VDD_DISPLAY_ID = "PSCCDD0";      // You will see it in registry (HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY)
static const char *VDD_DISPLAY_NAME = "ParsecVDA";  // You will see it in the [Advanced display settings] tab.

// Apdater GUID to obtain the device handle.
// {00b41627-04c4-429e-a26e-0265cf50c8fa}
static const GUID VDD_ADAPTER_GUID = { 0x00b41627, 0x04c4, 0x429e, { 0xa2, 0x6e, 0x02, 0x65, 0xcf, 0x50, 0xc8, 0xfa } };
static const char *VDD_ADAPTER_NAME = "Parsec Virtual Display Adapter";

// Class and hwid to query device status.
// {4d36e968-e325-11ce-bfc1-08002be10318}
static const GUID VDD_CLASS_GUID = { 0x4d36e968, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
static const char *VDD_HARDWARE_ID = "Root\\Parsec\\VDA";

// Actually up to 16 devices could be created per adapter
//  so just use a half to avoid plugging lag.
static const int VDD_MAX_DISPLAYS = 8;

// Core IoControl codes, see usage below.
typedef enum {
    VDD_IOCTL_ADD     = 0x0022e004, // CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 1, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
    VDD_IOCTL_REMOVE  = 0x0022a008, // CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
    VDD_IOCTL_UPDATE  = 0x0022a00c, // CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 3, METHOD_BUFFERED, FILE_WRITE_ACCESS)
    VDD_IOCTL_VERSION = 0x0022e010, // CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 4, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

    // new code in driver v0.45
    // relates to IOCTL_UPDATE and per display state
    // but unused in Parsec app
    VDD_IOCTL_UNKONWN = 0x0022a00c, // CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 5, METHOD_BUFFERED, FILE_WRITE_ACCESS)
} VddCtlCode;

// Generic DeviceIoControl for all IoControl codes.
static DWORD VddIoControl(HANDLE vdd, VddCtlCode code, const void *data, size_t size)
{
    if (vdd == NULL || vdd == INVALID_HANDLE_VALUE)
        return -1;

    BYTE InBuffer[32];
    ZeroMemory(InBuffer, sizeof(InBuffer));

    OVERLAPPED Overlapped;
    ZeroMemory(&Overlapped, sizeof(OVERLAPPED));

    DWORD OutBuffer = 0;
    DWORD NumberOfBytesTransferred;

    if (data != NULL && size > 0)
        memcpy(InBuffer, data, (size < sizeof(InBuffer)) ? size : sizeof(InBuffer));

    Overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    DeviceIoControl(vdd, (DWORD)code, InBuffer, sizeof(InBuffer), &OutBuffer, sizeof(DWORD), NULL, &Overlapped);

    if (!GetOverlappedResultEx(vdd, &Overlapped, &NumberOfBytesTransferred, 5000, FALSE))
    {
        CloseHandle(Overlapped.hEvent);
        return -1;
    }

    if (Overlapped.hEvent != NULL)
        CloseHandle(Overlapped.hEvent);

    return OutBuffer;
}

/**
* Query VDD minor version.
*
* @param vdd The device handle of VDD.
* @return The number of minor version.
*/
static int VddVersion(HANDLE vdd)
{
    int minor = VddIoControl(vdd, VDD_IOCTL_VERSION, NULL, 0);
    return minor;
}

/**
* Update/ping to VDD.
* Should call this function in a side thread for each
*   less than 100ms to keep all added virtual displays alive.
*
* @param vdd The device handle of VDD.
*/
static void VddUpdate(HANDLE vdd)
{
    VddIoControl(vdd, VDD_IOCTL_UPDATE, NULL, 0);
}

/**
* Add/plug a virtual display.
*
* @param vdd The device handle of VDD.
* @return The index of the added display.
*/
static int VddAddDisplay(HANDLE vdd)
{
    int idx = VddIoControl(vdd, VDD_IOCTL_ADD, NULL, 0);
    VddUpdate(vdd);

    return idx;
}

/**
* Remove/unplug a virtual display.
*
* @param vdd The device handle of VDD.
* @param index The index of the display will be removed.
*/
static void VddRemoveDisplay(HANDLE vdd, int index)
{
    // 16-bit BE index
    UINT16 indexData = ((index & 0xFF) << 8) | ((index >> 8) & 0xFF);

    VddIoControl(vdd, VDD_IOCTL_REMOVE, &indexData, sizeof(indexData));
    VddUpdate(vdd);
}

#ifdef __cplusplus
}
#endif

#endif