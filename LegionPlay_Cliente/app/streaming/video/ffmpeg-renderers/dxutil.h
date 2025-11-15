#pragma once

#include <Limelight.h>

class DXUtil
{
public:
    static bool isFormatHybridDecodedByHardware(int videoFormat, unsigned int vendorId, unsigned int deviceId) {
        if (vendorId == 0x8086) {
            // Intel seems to encode the series in the high byte of
            // the device ID. We want to avoid the "Partial" acceleration
            // support explicitly. Those will claim to have HW acceleration
            // but perform badly.
            // https://en.wikipedia.org/wiki/Intel_Graphics_Technology#Capabilities_(GPU_video_acceleration)
            // https://raw.githubusercontent.com/GameTechDev/gpudetect/master/IntelGfx.cfg
            switch (deviceId & 0xFF00) {
            case 0x0400: // Haswell
            case 0x0A00: // Haswell
            case 0x0D00: // Haswell
            case 0x1600: // Broadwell
            case 0x2200: // Cherry Trail and Braswell
                // Block these for HEVC to avoid hybrid decode
                return (videoFormat & VIDEO_FORMAT_MASK_H265) != 0;
            case 0x1900: // Skylake
                // Blacklist these for HEVC Main10 to avoid hybrid decode.
                // Regular HEVC Main is fine though.
                if (videoFormat == VIDEO_FORMAT_H265_MAIN10) {
                    return true;
                }
            default:
                break;
            }
        }
        else if (vendorId == 0x10DE) {
            // For NVIDIA, we wait to avoid those GPUs with Feature Set E
            // for HEVC decoding, since that's hybrid. It appears that Kepler GPUs
            // also had some hybrid decode support (per DXVA2 Checker) so we'll
            // blacklist those too.
            // https://en.wikipedia.org/wiki/Nvidia_PureVideo
            // https://bluesky23.yukishigure.com/en/dxvac/deviceInfo/decoder.html
            // http://envytools.readthedocs.io/en/latest/hw/pciid.html (missing GM200)
            if ((deviceId >= 0x1180 && deviceId <= 0x11BF) || // GK104
                    (deviceId >= 0x11C0 && deviceId <= 0x11FF) || // GK106
                    (deviceId >= 0x0FC0 && deviceId <= 0x0FFF) || // GK107
                    (deviceId >= 0x1000 && deviceId <= 0x103F) || // GK110/GK110B
                    (deviceId >= 0x1280 && deviceId <= 0x12BF) || // GK208
                    (deviceId >= 0x1340 && deviceId <= 0x137F) || // GM108
                    (deviceId >= 0x1380 && deviceId <= 0x13BF) || // GM107
                    (deviceId >= 0x13C0 && deviceId <= 0x13FF) || // GM204
                    (deviceId >= 0x1617 && deviceId <= 0x161A) || // GM204
                    (deviceId == 0x1667) || // GM204
                    (deviceId >= 0x17C0 && deviceId <= 0x17FF)) { // GM200
                // Avoid HEVC on Feature Set E GPUs
                return (videoFormat & VIDEO_FORMAT_MASK_H265) != 0;
            }
        }

        return false;
    }
};
