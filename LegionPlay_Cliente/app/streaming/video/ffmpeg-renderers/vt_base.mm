// Nasty hack to avoid conflict between AVFoundation and
// libavutil both defining AVMediaType
#define AVMediaType AVMediaType_FFmpeg
#include "vt.h"
#undef AVMediaType

#import <Cocoa/Cocoa.h>
#import <VideoToolbox/VideoToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <Metal/Metal.h>

#include <mach/machine.h>
#include <sys/sysctl.h>

VTBaseRenderer::VTBaseRenderer(IFFmpegRenderer::RendererType type) :
    IFFmpegRenderer(type),
    m_HdrMetadataChanged(false),
    m_MasteringDisplayColorVolume(nullptr),
    m_ContentLightLevelInfo(nullptr) {

}

VTBaseRenderer::~VTBaseRenderer() {
    if (m_MasteringDisplayColorVolume != nullptr) {
        CFRelease(m_MasteringDisplayColorVolume);
    }

    if (m_ContentLightLevelInfo != nullptr) {
        CFRelease(m_ContentLightLevelInfo);
    }
}

bool VTBaseRenderer::isAppleSilicon() {
    static uint32_t cpuType = 0;
    if (cpuType == 0) {
        size_t size = sizeof(cpuType);
        int err = sysctlbyname("hw.cputype", &cpuType, &size, NULL, 0);
        if (err != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "sysctlbyname(hw.cputype) failed: %d", err);
            return false;
        }
    }

    // Apple Silicon Macs have CPU_ARCH_ABI64 set, so we need to mask that off.
    // For some reason, 64-bit Intel Macs don't seem to have CPU_ARCH_ABI64 set.
    return (cpuType & ~CPU_ARCH_MASK) == CPU_TYPE_ARM;
}

bool VTBaseRenderer::checkDecoderCapabilities(id<MTLDevice> device, PDECODER_PARAMETERS params) {
    if (params->videoFormat & VIDEO_FORMAT_MASK_H264) {
        if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_H264)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "No HW accelerated H.264 decode via VT");
            return false;
        }
    }
    else if (params->videoFormat & VIDEO_FORMAT_MASK_H265) {
        if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "No HW accelerated HEVC decode via VT");
            return false;
        }

        // HEVC Main10 requires more extensive checks because there's no
        // simple API to check for Main10 hardware decoding, and if we don't
        // have it, we'll silently get software decoding with horrible performance.
        if (params->videoFormat == VIDEO_FORMAT_H265_MAIN10) {
            // Exclude all GPUs earlier than macOSGPUFamily2
            // https://developer.apple.com/documentation/metal/mtlfeatureset/mtlfeatureset_macos_gpufamily2_v1
            if ([device supportsFamily:MTLGPUFamilyMac2]) {
                if ([device.name containsString:@"Intel"]) {
                    // 500-series Intel GPUs are Skylake and don't support Main10 hardware decoding
                    if ([device.name containsString:@" 5"]) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "No HEVC Main10 support on Skylake iGPU");
                        return false;
                    }
                }
                else if ([device.name containsString:@"AMD"]) {
                    // FirePro D, M200, and M300 series GPUs don't support Main10 hardware decoding
                    if ([device.name containsString:@"FirePro D"] ||
                            [device.name containsString:@" M2"] ||
                            [device.name containsString:@" M3"]) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "No HEVC Main10 support on AMD GPUs until Polaris");
                        return false;
                    }
                }
            }
            else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "No HEVC Main10 support on macOS GPUFamily1 GPUs");
                return false;
            }
        }
    }
    else if (params->videoFormat & VIDEO_FORMAT_MASK_AV1) {
    #if __MAC_OS_X_VERSION_MAX_ALLOWED >= 130000
        if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_AV1)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "No HW accelerated AV1 decode via VT");
            return false;
        }

        // 10-bit is part of the Main profile for AV1, so it will always
        // be present on hardware that supports 8-bit.
    #else
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "AV1 requires building with Xcode 14 or later");
        return false;
    #endif
    }

    return true;
}

void VTBaseRenderer::setHdrMode(bool enabled) {
    // Free existing HDR metadata
    if (m_MasteringDisplayColorVolume != nullptr) {
        CFRelease(m_MasteringDisplayColorVolume);
        m_MasteringDisplayColorVolume = nullptr;
    }
    if (m_ContentLightLevelInfo != nullptr) {
        CFRelease(m_ContentLightLevelInfo);
        m_ContentLightLevelInfo = nullptr;
    }

    // Store new HDR metadata if available
    SS_HDR_METADATA hdrMetadata;
    if (enabled && LiGetHdrMetadata(&hdrMetadata)) {
        if (hdrMetadata.displayPrimaries[0].x != 0 && hdrMetadata.maxDisplayLuminance != 0) {
            // This data is all in big-endian
            struct {
              vector_ushort2 primaries[3];
              vector_ushort2 white_point;
              uint32_t luminance_max;
              uint32_t luminance_min;
            } __attribute__((packed, aligned(4))) mdcv;

            // mdcv is in GBR order while SS_HDR_METADATA is in RGB order
            mdcv.primaries[0].x = __builtin_bswap16(hdrMetadata.displayPrimaries[1].x);
            mdcv.primaries[0].y = __builtin_bswap16(hdrMetadata.displayPrimaries[1].y);
            mdcv.primaries[1].x = __builtin_bswap16(hdrMetadata.displayPrimaries[2].x);
            mdcv.primaries[1].y = __builtin_bswap16(hdrMetadata.displayPrimaries[2].y);
            mdcv.primaries[2].x = __builtin_bswap16(hdrMetadata.displayPrimaries[0].x);
            mdcv.primaries[2].y = __builtin_bswap16(hdrMetadata.displayPrimaries[0].y);

            mdcv.white_point.x = __builtin_bswap16(hdrMetadata.whitePoint.x);
            mdcv.white_point.y = __builtin_bswap16(hdrMetadata.whitePoint.y);

            // These luminance values are in 10000ths of a nit
            mdcv.luminance_max = __builtin_bswap32((uint32_t)hdrMetadata.maxDisplayLuminance * 10000);
            mdcv.luminance_min = __builtin_bswap32(hdrMetadata.minDisplayLuminance);

            m_MasteringDisplayColorVolume = CFDataCreate(nullptr, (const UInt8*)&mdcv, sizeof(mdcv));
        }

        if (hdrMetadata.maxContentLightLevel != 0 && hdrMetadata.maxFrameAverageLightLevel != 0) {
            // This data is all in big-endian
            struct {
                uint16_t max_content_light_level;
                uint16_t max_frame_average_light_level;
            } __attribute__((packed, aligned(2))) cll;

            cll.max_content_light_level = __builtin_bswap16(hdrMetadata.maxContentLightLevel);
            cll.max_frame_average_light_level = __builtin_bswap16(hdrMetadata.maxFrameAverageLightLevel);

            m_ContentLightLevelInfo = CFDataCreate(nullptr, (const UInt8*)&cll, sizeof(cll));
        }
    }

    m_HdrMetadataChanged = true;
}
