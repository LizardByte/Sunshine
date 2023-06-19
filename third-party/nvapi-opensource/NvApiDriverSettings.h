/*****************************************************************************\
|*                                                                             *|
|* Copyright (c) 2019-2022, NVIDIA CORPORATION. All rights reserved.           *|
|*                                                                             *|
|* Permission is hereby granted, free of charge, to any person obtaining a     *|
|* copy of this software and associated documentation files (the "Software"),  *|
|* to deal in the Software without restriction, including without limitation   *|
|* the rights to use, copy, modify, merge, publish, distribute, sublicense,    *|
|* and/or sell copies of the Software, and to permit persons to whom the       *|
|* Software is furnished to do so, subject to the following conditions:        *|
|*                                                                             *|
|* The above copyright notice and this permission notice shall be included in  *|
|* all copies or substantial portions of the Software.                         *|
|*                                                                             *|
|* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR  *|
|* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,    *|
|* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL    *|
|* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER  *|
|* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING     *|
|* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER         *|
|* DEALINGS IN THE SOFTWARE.                                                   *|
|*                                                                             *|
|*                                                                             *|
\*****************************************************************************/

#ifndef _NVAPI_DRIVER_SETTINGS_H_
#define _NVAPI_DRIVER_SETTINGS_H_

#define OGL_AA_LINE_GAMMA_STRING                   L"Antialiasing - Line gamma"
#define OGL_CPL_GDI_COMPATIBILITY_STRING           L"OpenGL GDI compatibility"
#define OGL_CPL_PREFER_DXPRESENT_STRING            L"Vulkan/OpenGL present method"
#define OGL_DEEP_COLOR_SCANOUT_STRING              L"Deep color for 3D applications"
#define OGL_DEFAULT_SWAP_INTERVAL_STRING           L"OpenGL default swap interval"
#define OGL_DEFAULT_SWAP_INTERVAL_FRACTIONAL_STRING L"OpenGL default swap interval fraction"
#define OGL_DEFAULT_SWAP_INTERVAL_SIGN_STRING      L"OpenGL default swap interval sign"
#define OGL_EVENT_LOG_SEVERITY_THRESHOLD_STRING    L"Event Log Severity Threshold"
#define OGL_EXTENSION_STRING_VERSION_STRING        L"Extension String version"
#define OGL_FORCE_BLIT_STRING                      L"Buffer-flipping mode"
#define OGL_FORCE_STEREO_STRING                    L"Force Stereo shuttering"
#define OGL_IMPLICIT_GPU_AFFINITY_STRING           L"Preferred OpenGL GPU"
#define OGL_MAX_FRAMES_ALLOWED_STRING              L"Maximum frames allowed"
#define OGL_OVERLAY_PIXEL_TYPE_STRING              L"Exported Overlay pixel types"
#define OGL_OVERLAY_SUPPORT_STRING                 L"Enable overlay"
#define OGL_QUALITY_ENHANCEMENTS_STRING            L"High level control of the rendering quality on OpenGL"
#define OGL_SINGLE_BACKDEPTH_BUFFER_STRING         L"Unified back/depth buffer"
#define OGL_SLI_CFR_MODE_STRING                    L"Set CFR mode"
#define OGL_SLI_MULTICAST_STRING                   L"Enable NV_gpu_multicast extension"
#define OGL_THREAD_CONTROL_STRING                  L"Threaded optimization"
#define OGL_TMON_LEVEL_STRING                      L"Event Log Tmon Severity Threshold"
#define OGL_TRIPLE_BUFFER_STRING                   L"Triple buffering"
#define AA_BEHAVIOR_FLAGS_STRING                   L"Antialiasing - Behavior Flags"
#define AA_MODE_ALPHATOCOVERAGE_STRING             L"Antialiasing - Transparency Multisampling"
#define AA_MODE_GAMMACORRECTION_STRING             L"Antialiasing - Gamma correction"
#define AA_MODE_METHOD_STRING                      L"Antialiasing - Setting"
#define AA_MODE_REPLAY_STRING                      L"Antialiasing - Transparency Supersampling"
#define AA_MODE_SELECTOR_STRING                    L"Antialiasing - Mode"
#define AA_MODE_SELECTOR_SLIAA_STRING              L"Antialiasing - SLI AA"
#define ANISO_MODE_LEVEL_STRING                    L"Anisotropic filtering setting"
#define ANISO_MODE_SELECTOR_STRING                 L"Anisotropic filtering mode"
#define ANSEL_ALLOW_STRING                         L"NVIDIA Predefined Ansel Usage"
#define ANSEL_ALLOWLISTED_STRING                   L"Ansel flags for enabled applications"
#define ANSEL_ENABLE_STRING                        L"Enable Ansel"
#define APPLICATION_PROFILE_NOTIFICATION_TIMEOUT_STRING L"Application Profile Notification Popup Timeout"
#define APPLICATION_STEAM_ID_STRING                L"Steam Application ID"
#define BATTERY_BOOST_APP_FPS_STRING               L"Battery Boost Application FPS"
#define CPL_HIDDEN_PROFILE_STRING                  L"Do not display this profile in the Control Panel"
#define CUDA_EXCLUDED_GPUS_STRING                  L"List of Universal GPU ids"
#define D3DOGL_GPU_MAX_POWER_STRING                L"Maximum GPU Power"
#define EXPORT_PERF_COUNTERS_STRING                L"Export Performance Counters"
#define EXTERNAL_QUIET_MODE_STRING                 L"External Quiet Mode (XQM)"
#define FRL_FPS_STRING                             L"Frame Rate Limiter"
#define FXAA_ALLOW_STRING                          L"NVIDIA Predefined FXAA Usage"
#define FXAA_ENABLE_STRING                         L"Enable FXAA"
#define FXAA_INDICATOR_ENABLE_STRING               L"Enable FXAA Indicator"
#define LATENCY_INDICATOR_AUTOALIGN_STRING         L"Autoalign flash indicator"
#define MCSFRSHOWSPLIT_STRING                      L"SLI indicator"
#define NV_QUALITY_UPSCALING_STRING                L"NVIDIA Quality upscaling"
#define OPTIMUS_MAXAA_STRING                       L"Maximum AA samples allowed for a given application"
#define PHYSXINDICATOR_STRING                      L"Display the PhysX indicator"
#define PREFERRED_PSTATE_STRING                    L"Power management mode"
#define PREVENT_UI_AF_OVERRIDE_STRING              L"No override of Anisotropic filtering"
#define SHIM_MAXRES_STRING                         L"Maximum resolution allowed for a given application"
#define SHIM_MCCOMPAT_STRING                       L"Optimus flags for enabled applications"
#define SHIM_RENDERING_MODE_STRING                 L"Enable application for Optimus"
#define SHIM_RENDERING_OPTIONS_STRING              L"Shim Rendering Mode Options per application for Optimus"
#define SLI_GPU_COUNT_STRING                       L"Number of GPUs to use on SLI rendering mode"
#define SLI_PREDEFINED_GPU_COUNT_STRING            L"NVIDIA predefined number of GPUs to use on SLI rendering mode"
#define SLI_PREDEFINED_GPU_COUNT_DX10_STRING       L"NVIDIA predefined number of GPUs to use on SLI rendering mode on DirectX 10"
#define SLI_PREDEFINED_MODE_STRING                 L"NVIDIA predefined SLI mode"
#define SLI_PREDEFINED_MODE_DX10_STRING            L"NVIDIA predefined SLI mode on DirectX 10"
#define SLI_RENDERING_MODE_STRING                  L"SLI rendering mode"
#define VRPRERENDERLIMIT_STRING                    L"Virtual Reality pre-rendered frames"
#define VRRFEATUREINDICATOR_STRING                 L"Toggle the VRR global feature"
#define VRROVERLAYINDICATOR_STRING                 L"Display the VRR Overlay Indicator"
#define VRRREQUESTSTATE_STRING                     L"VRR requested state"
#define VRR_APP_OVERRIDE_STRING                    L"G-SYNC"
#define VRR_APP_OVERRIDE_REQUEST_STATE_STRING      L"G-SYNC"
#define VRR_MODE_STRING                            L"Enable G-SYNC globally"
#define VSYNCSMOOTHAFR_STRING                      L"Flag to control smooth AFR behavior"
#define VSYNCVRRCONTROL_STRING                     L"Variable refresh Rate"
#define VSYNC_BEHAVIOR_FLAGS_STRING                L"Vsync - Behavior Flags"
#define WKS_API_STEREO_EYES_EXCHANGE_STRING        L"Stereo - Swap eyes"
#define WKS_API_STEREO_MODE_STRING                 L"Stereo - Display mode"
#define WKS_MEMORY_ALLOCATION_POLICY_STRING        L"Memory Allocation Policy"
#define WKS_STEREO_DONGLE_SUPPORT_STRING           L"Stereo - Dongle Support"
#define WKS_STEREO_SUPPORT_STRING                  L"Stereo - Enable"
#define WKS_STEREO_SWAP_MODE_STRING                L"Stereo - swap mode"
#define AO_MODE_STRING                             L"Ambient Occlusion"
#define AO_MODE_ACTIVE_STRING                      L"NVIDIA Predefined Ambient Occlusion Usage"
#define AUTO_LODBIASADJUST_STRING                  L"Texture filtering - Driver Controlled LOD Bias"
#define EXPORT_PERF_COUNTERS_DX9_ONLY_STRING       L"Export Performance Counters for DX9 only"
#define ICAFE_LOGO_CONFIG_STRING                   L"ICafe Settings"
#define LODBIASADJUST_STRING                       L"Texture filtering - LOD Bias"
#define MAXWELL_B_SAMPLE_INTERLEAVE_STRING         L"Enable sample interleaving (MFAA)"
#define PRERENDERLIMIT_STRING                      L"Maximum pre-rendered frames"
#define PS_SHADERDISKCACHE_STRING                  L"Shader Cache"
#define PS_SHADERDISKCACHE_MAX_SIZE_STRING         L"Shader disk cache maximum size"
#define PS_TEXFILTER_ANISO_OPTS2_STRING            L"Texture filtering - Anisotropic sample optimization"
#define PS_TEXFILTER_BILINEAR_IN_ANISO_STRING      L"Texture filtering - Anisotropic filter optimization"
#define PS_TEXFILTER_DISABLE_TRILIN_SLOPE_STRING   L"Texture filtering - Trilinear optimization"
#define PS_TEXFILTER_NO_NEG_LODBIAS_STRING         L"Texture filtering - Negative LOD bias"
#define QUALITY_ENHANCEMENTS_STRING                L"Texture filtering - Quality"
#define QUALITY_ENHANCEMENT_SUBSTITUTION_STRING    L"Texture filtering - Quality Substitution"
#define REFRESH_RATE_OVERRIDE_STRING               L"Preferred refresh rate"
#define SET_POWER_THROTTLE_FOR_PCIe_COMPLIANCE_STRING L"PowerThrottle"
#define SET_VAB_DATA_STRING                        L"VAB Default Data"
#define VSYNCMODE_STRING                           L"Vertical Sync"
#define VSYNCTEARCONTROL_STRING                    L"Vertical Sync Tear Control"

enum ESetting {
    OGL_AA_LINE_GAMMA_ID                          = 0x2089BF6C,
    OGL_CPL_GDI_COMPATIBILITY_ID                  = 0x2072C5A3,
    OGL_CPL_PREFER_DXPRESENT_ID                   = 0x20D690F8,
    OGL_DEEP_COLOR_SCANOUT_ID                     = 0x2097C2F6,
    OGL_DEFAULT_SWAP_INTERVAL_ID                  = 0x206A6582,
    OGL_DEFAULT_SWAP_INTERVAL_FRACTIONAL_ID       = 0x206C4581,
    OGL_DEFAULT_SWAP_INTERVAL_SIGN_ID             = 0x20655CFA,
    OGL_EVENT_LOG_SEVERITY_THRESHOLD_ID           = 0x209DF23E,
    OGL_EXTENSION_STRING_VERSION_ID               = 0x20FF7493,
    OGL_FORCE_BLIT_ID                             = 0x201F619F,
    OGL_FORCE_STEREO_ID                           = 0x204D9A0C,
    OGL_IMPLICIT_GPU_AFFINITY_ID                  = 0x20D0F3E6,
    OGL_MAX_FRAMES_ALLOWED_ID                     = 0x208E55E3,
    OGL_OVERLAY_PIXEL_TYPE_ID                     = 0x209AE66F,
    OGL_OVERLAY_SUPPORT_ID                        = 0x206C28C4,
    OGL_QUALITY_ENHANCEMENTS_ID                   = 0x20797D6C,
    OGL_SINGLE_BACKDEPTH_BUFFER_ID                = 0x20A29055,
    OGL_SLI_CFR_MODE_ID                           = 0x20343843,
    OGL_SLI_MULTICAST_ID                          = 0x2092D3BE,
    OGL_THREAD_CONTROL_ID                         = 0x20C1221E,
    OGL_TMON_LEVEL_ID                             = 0x202888C1,
    OGL_TRIPLE_BUFFER_ID                          = 0x20FDD1F9,
    AA_BEHAVIOR_FLAGS_ID                          = 0x10ECDB82,
    AA_MODE_ALPHATOCOVERAGE_ID                    = 0x10FC2D9C,
    AA_MODE_GAMMACORRECTION_ID                    = 0x107D639D,
    AA_MODE_METHOD_ID                             = 0x10D773D2,
    AA_MODE_REPLAY_ID                             = 0x10D48A85,
    AA_MODE_SELECTOR_ID                           = 0x107EFC5B,
    AA_MODE_SELECTOR_SLIAA_ID                     = 0x107AFC5B,
    ANISO_MODE_LEVEL_ID                           = 0x101E61A9,
    ANISO_MODE_SELECTOR_ID                        = 0x10D2BB16,
    ANSEL_ALLOW_ID                                = 0x1035DB89,
    ANSEL_ALLOWLISTED_ID                          = 0x1085DA8A,
    ANSEL_ENABLE_ID                               = 0x1075D972,
    APPLICATION_PROFILE_NOTIFICATION_TIMEOUT_ID   = 0x104554B6,
    APPLICATION_STEAM_ID_ID                       = 0x107CDDBC,
    BATTERY_BOOST_APP_FPS_ID                      = 0x10115C8C,
    CPL_HIDDEN_PROFILE_ID                         = 0x106D5CFF,
    CUDA_EXCLUDED_GPUS_ID                         = 0x10354FF8,
    D3DOGL_GPU_MAX_POWER_ID                       = 0x10D1EF29,
    EXPORT_PERF_COUNTERS_ID                       = 0x108F0841,
    EXTERNAL_QUIET_MODE_ID                        = 0x10115C8D,
    FRL_FPS_ID                                    = 0x10835002,
    FXAA_ALLOW_ID                                 = 0x1034CB89,
    FXAA_ENABLE_ID                                = 0x1074C972,
    FXAA_INDICATOR_ENABLE_ID                      = 0x1068FB9C,
    LATENCY_INDICATOR_AUTOALIGN_ID                = 0x1095F170,
    MCSFRSHOWSPLIT_ID                             = 0x10287051,
    NV_QUALITY_UPSCALING_ID                       = 0x10444444,
    OPTIMUS_MAXAA_ID                              = 0x10F9DC83,
    PHYSXINDICATOR_ID                             = 0x1094F16F,
    PREFERRED_PSTATE_ID                           = 0x1057EB71,
    PREVENT_UI_AF_OVERRIDE_ID                     = 0x103BCCB5,
    SHIM_MAXRES_ID                                = 0x10F9DC82,
    SHIM_MCCOMPAT_ID                              = 0x10F9DC80,
    SHIM_RENDERING_MODE_ID                        = 0x10F9DC81,
    SHIM_RENDERING_OPTIONS_ID                     = 0x10F9DC84,
    SLI_GPU_COUNT_ID                              = 0x1033DCD1,
    SLI_PREDEFINED_GPU_COUNT_ID                   = 0x1033DCD2,
    SLI_PREDEFINED_GPU_COUNT_DX10_ID              = 0x1033DCD3,
    SLI_PREDEFINED_MODE_ID                        = 0x1033CEC1,
    SLI_PREDEFINED_MODE_DX10_ID                   = 0x1033CEC2,
    SLI_RENDERING_MODE_ID                         = 0x1033CED1,
    VRPRERENDERLIMIT_ID                           = 0x10111133,
    VRRFEATUREINDICATOR_ID                        = 0x1094F157,
    VRROVERLAYINDICATOR_ID                        = 0x1095F16F,
    VRRREQUESTSTATE_ID                            = 0x1094F1F7,
    VRR_APP_OVERRIDE_ID                           = 0x10A879CF,
    VRR_APP_OVERRIDE_REQUEST_STATE_ID             = 0x10A879AC,
    VRR_MODE_ID                                   = 0x1194F158,
    VSYNCSMOOTHAFR_ID                             = 0x101AE763,
    VSYNCVRRCONTROL_ID                            = 0x10A879CE,
    VSYNC_BEHAVIOR_FLAGS_ID                       = 0x10FDEC23,
    WKS_API_STEREO_EYES_EXCHANGE_ID               = 0x11AE435C,
    WKS_API_STEREO_MODE_ID                        = 0x11E91A61,
    WKS_MEMORY_ALLOCATION_POLICY_ID               = 0x11112233,
    WKS_STEREO_DONGLE_SUPPORT_ID                  = 0x112493BD,
    WKS_STEREO_SUPPORT_ID                         = 0x11AA9E99,
    WKS_STEREO_SWAP_MODE_ID                       = 0x11333333,
    AO_MODE_ID                                    = 0x00667329,
    AO_MODE_ACTIVE_ID                             = 0x00664339,
    AUTO_LODBIASADJUST_ID                         = 0x00638E8F,
    EXPORT_PERF_COUNTERS_DX9_ONLY_ID              = 0x00B65E72,
    ICAFE_LOGO_CONFIG_ID                          = 0x00DB1337,
    LODBIASADJUST_ID                              = 0x00738E8F,
    MAXWELL_B_SAMPLE_INTERLEAVE_ID                = 0x0098C1AC,
    PRERENDERLIMIT_ID                             = 0x007BA09E,
    PS_SHADERDISKCACHE_ID                         = 0x00198FFF,
    PS_SHADERDISKCACHE_MAX_SIZE_ID                = 0x00AC8497,
    PS_TEXFILTER_ANISO_OPTS2_ID                   = 0x00E73211,
    PS_TEXFILTER_BILINEAR_IN_ANISO_ID             = 0x0084CD70,
    PS_TEXFILTER_DISABLE_TRILIN_SLOPE_ID          = 0x002ECAF2,
    PS_TEXFILTER_NO_NEG_LODBIAS_ID                = 0x0019BB68,
    QUALITY_ENHANCEMENTS_ID                       = 0x00CE2691,
    QUALITY_ENHANCEMENT_SUBSTITUTION_ID           = 0x00CE2692,
    REFRESH_RATE_OVERRIDE_ID                      = 0x0064B541,
    SET_POWER_THROTTLE_FOR_PCIe_COMPLIANCE_ID     = 0x00AE785C,
    SET_VAB_DATA_ID                               = 0x00AB8687,
    VSYNCMODE_ID                                  = 0x00A879CF,
    VSYNCTEARCONTROL_ID                           = 0x005A375C,
    TOTAL_DWORD_SETTING_NUM = 96,
    TOTAL_WSTRING_SETTING_NUM = 4,
    TOTAL_SETTING_NUM = 100,
    INVALID_SETTING_ID = 0xFFFFFFFF
};

enum EValues_OGL_AA_LINE_GAMMA {
    OGL_AA_LINE_GAMMA_DISABLED                           = 0x10,
    OGL_AA_LINE_GAMMA_ENABLED                            = 0x23,
    OGL_AA_LINE_GAMMA_MIN                                = 1,
    OGL_AA_LINE_GAMMA_MAX                                = 100,
    OGL_AA_LINE_GAMMA_NUM_VALUES = 4,
    OGL_AA_LINE_GAMMA_DEFAULT = OGL_AA_LINE_GAMMA_DISABLED
};

enum EValues_OGL_CPL_GDI_COMPATIBILITY {
    OGL_CPL_GDI_COMPATIBILITY_PREFER_DISABLED            = 0x00000000,
    OGL_CPL_GDI_COMPATIBILITY_PREFER_ENABLED             = 0x00000001,
    OGL_CPL_GDI_COMPATIBILITY_AUTO                       = 0x00000002,
    OGL_CPL_GDI_COMPATIBILITY_NUM_VALUES = 3,
    OGL_CPL_GDI_COMPATIBILITY_DEFAULT = OGL_CPL_GDI_COMPATIBILITY_AUTO
};

enum EValues_OGL_CPL_PREFER_DXPRESENT {
    OGL_CPL_PREFER_DXPRESENT_PREFER_DISABLED             = 0x00000000,
    OGL_CPL_PREFER_DXPRESENT_PREFER_ENABLED              = 0x00000001,
    OGL_CPL_PREFER_DXPRESENT_AUTO                        = 0x00000002,
    OGL_CPL_PREFER_DXPRESENT_NUM_VALUES = 3,
    OGL_CPL_PREFER_DXPRESENT_DEFAULT = OGL_CPL_PREFER_DXPRESENT_AUTO
};

enum EValues_OGL_DEEP_COLOR_SCANOUT {
    OGL_DEEP_COLOR_SCANOUT_DISABLE                       = 0,
    OGL_DEEP_COLOR_SCANOUT_ENABLE                        = 1,
    OGL_DEEP_COLOR_SCANOUT_NUM_VALUES = 2,
    OGL_DEEP_COLOR_SCANOUT_DEFAULT = OGL_DEEP_COLOR_SCANOUT_ENABLE
};

enum EValues_OGL_DEFAULT_SWAP_INTERVAL {
    OGL_DEFAULT_SWAP_INTERVAL_TEAR                       = 0,
    OGL_DEFAULT_SWAP_INTERVAL_VSYNC_ONE                  = 1,
    OGL_DEFAULT_SWAP_INTERVAL_VSYNC                      = 1,
    OGL_DEFAULT_SWAP_INTERVAL_VALUE_MASK                 = 0x0000FFFF,
    OGL_DEFAULT_SWAP_INTERVAL_FORCE_MASK                 = 0xF0000000,
    OGL_DEFAULT_SWAP_INTERVAL_FORCE_OFF                  = 0xF0000000,
    OGL_DEFAULT_SWAP_INTERVAL_FORCE_ON                   = 0x10000000,
    OGL_DEFAULT_SWAP_INTERVAL_APP_CONTROLLED             = 0x00000000,
    OGL_DEFAULT_SWAP_INTERVAL_DISABLE                    = 0xffffffff,
    OGL_DEFAULT_SWAP_INTERVAL_NUM_VALUES = 9,
    OGL_DEFAULT_SWAP_INTERVAL_DEFAULT = OGL_DEFAULT_SWAP_INTERVAL_VSYNC_ONE
};

enum EValues_OGL_DEFAULT_SWAP_INTERVAL_FRACTIONAL {
    OGL_DEFAULT_SWAP_INTERVAL_FRACTIONAL_ZERO_SCANLINES  = 0,
    OGL_DEFAULT_SWAP_INTERVAL_FRACTIONAL_ONE_FULL_FRAME_OF_SCANLINES = 100,
    OGL_DEFAULT_SWAP_INTERVAL_FRACTIONAL_NUM_VALUES = 2,
    OGL_DEFAULT_SWAP_INTERVAL_FRACTIONAL_DEFAULT = 0U
};

enum EValues_OGL_DEFAULT_SWAP_INTERVAL_SIGN {
    OGL_DEFAULT_SWAP_INTERVAL_SIGN_POSITIVE              = 0,
    OGL_DEFAULT_SWAP_INTERVAL_SIGN_NEGATIVE              = 1,
    OGL_DEFAULT_SWAP_INTERVAL_SIGN_NUM_VALUES = 2,
    OGL_DEFAULT_SWAP_INTERVAL_SIGN_DEFAULT = OGL_DEFAULT_SWAP_INTERVAL_SIGN_POSITIVE
};

enum EValues_OGL_EVENT_LOG_SEVERITY_THRESHOLD {
    OGL_EVENT_LOG_SEVERITY_THRESHOLD_DISABLE             = 0,
    OGL_EVENT_LOG_SEVERITY_THRESHOLD_CRITICAL            = 1,
    OGL_EVENT_LOG_SEVERITY_THRESHOLD_WARNING             = 2,
    OGL_EVENT_LOG_SEVERITY_THRESHOLD_INFORMATION         = 3,
    OGL_EVENT_LOG_SEVERITY_THRESHOLD_ALL                 = 4,
    OGL_EVENT_LOG_SEVERITY_THRESHOLD_NUM_VALUES = 5,
    OGL_EVENT_LOG_SEVERITY_THRESHOLD_DEFAULT = OGL_EVENT_LOG_SEVERITY_THRESHOLD_ALL
};

enum EValues_OGL_FORCE_BLIT {
    OGL_FORCE_BLIT_ON                                    = 1,
    OGL_FORCE_BLIT_OFF                                   = 0,
    OGL_FORCE_BLIT_NUM_VALUES = 2,
    OGL_FORCE_BLIT_DEFAULT = OGL_FORCE_BLIT_OFF
};

enum EValues_OGL_FORCE_STEREO {
    OGL_FORCE_STEREO_OFF                                 = 0,
    OGL_FORCE_STEREO_ON                                  = 1,
    OGL_FORCE_STEREO_NUM_VALUES = 2,
    OGL_FORCE_STEREO_DEFAULT = OGL_FORCE_STEREO_OFF
};

#define    OGL_IMPLICIT_GPU_AFFINITY_ENV_VAR                    L"OGL_DEFAULT_RENDERING_GPU"
#define    OGL_IMPLICIT_GPU_AFFINITY_AUTOSELECT                 L"autoselect"
#define    OGL_IMPLICIT_GPU_AFFINITY_NUM_VALUES 1
#define    OGL_IMPLICIT_GPU_AFFINITY_DEFAULT OGL_IMPLICIT_GPU_AFFINITY_AUTOSELECT

enum EValues_OGL_OVERLAY_PIXEL_TYPE {
    OGL_OVERLAY_PIXEL_TYPE_NONE                          = 0x0,
    OGL_OVERLAY_PIXEL_TYPE_CI                            = 0x1,
    OGL_OVERLAY_PIXEL_TYPE_RGBA                          = 0x2,
    OGL_OVERLAY_PIXEL_TYPE_CI_AND_RGBA                   = 0x3,
    OGL_OVERLAY_PIXEL_TYPE_NUM_VALUES = 4,
    OGL_OVERLAY_PIXEL_TYPE_DEFAULT = OGL_OVERLAY_PIXEL_TYPE_CI
};

enum EValues_OGL_OVERLAY_SUPPORT {
    OGL_OVERLAY_SUPPORT_OFF                              = 0,
    OGL_OVERLAY_SUPPORT_ON                               = 1,
    OGL_OVERLAY_SUPPORT_FORCE_SW                         = 2,
    OGL_OVERLAY_SUPPORT_NUM_VALUES = 3,
    OGL_OVERLAY_SUPPORT_DEFAULT = OGL_OVERLAY_SUPPORT_OFF
};

enum EValues_OGL_QUALITY_ENHANCEMENTS {
    OGL_QUALITY_ENHANCEMENTS_HQUAL                       = 0xfffffff6,
    OGL_QUALITY_ENHANCEMENTS_QUAL                        = 0,
    OGL_QUALITY_ENHANCEMENTS_PERF                        = 10,
    OGL_QUALITY_ENHANCEMENTS_HPERF                       = 20,
    OGL_QUALITY_ENHANCEMENTS_NUM_VALUES = 4,
    OGL_QUALITY_ENHANCEMENTS_DEFAULT = OGL_QUALITY_ENHANCEMENTS_QUAL
};

enum EValues_OGL_SINGLE_BACKDEPTH_BUFFER {
    OGL_SINGLE_BACKDEPTH_BUFFER_DISABLE                  = 0x0,
    OGL_SINGLE_BACKDEPTH_BUFFER_ENABLE                   = 0x1,
    OGL_SINGLE_BACKDEPTH_BUFFER_USE_HW_DEFAULT           = 0xffffffff,
    OGL_SINGLE_BACKDEPTH_BUFFER_NUM_VALUES = 3,
    OGL_SINGLE_BACKDEPTH_BUFFER_DEFAULT = OGL_SINGLE_BACKDEPTH_BUFFER_DISABLE
};

enum EValues_OGL_SLI_CFR_MODE {
    OGL_SLI_CFR_MODE_DISABLE                             = 0x00,
    OGL_SLI_CFR_MODE_ENABLE                              = 0x01,
    OGL_SLI_CFR_MODE_CLASSIC_SFR                         = 0x02,
    OGL_SLI_CFR_MODE_NUM_VALUES = 3,
    OGL_SLI_CFR_MODE_DEFAULT = OGL_SLI_CFR_MODE_DISABLE
};

enum EValues_OGL_SLI_MULTICAST {
    OGL_SLI_MULTICAST_DISABLE                            = 0x00,
    OGL_SLI_MULTICAST_ENABLE                             = 0x01,
    OGL_SLI_MULTICAST_FORCE_DISABLE                      = 0x02,
    OGL_SLI_MULTICAST_ALLOW_MOSAIC                       = 0x04,
    OGL_SLI_MULTICAST_NUM_VALUES = 4,
    OGL_SLI_MULTICAST_DEFAULT = OGL_SLI_MULTICAST_DISABLE
};

enum EValues_OGL_THREAD_CONTROL {
    OGL_THREAD_CONTROL_ENABLE                            = 0x00000001,
    OGL_THREAD_CONTROL_DISABLE                           = 0x00000002,
    OGL_THREAD_CONTROL_NUM_VALUES = 2,
    OGL_THREAD_CONTROL_DEFAULT = 0U
};

enum EValues_OGL_TMON_LEVEL {
    OGL_TMON_LEVEL_DISABLE                               = 0,
    OGL_TMON_LEVEL_CRITICAL                              = 1,
    OGL_TMON_LEVEL_WARNING                               = 2,
    OGL_TMON_LEVEL_INFORMATION                           = 3,
    OGL_TMON_LEVEL_MOST                                  = 4,
    OGL_TMON_LEVEL_VERBOSE                               = 5,
    OGL_TMON_LEVEL_NUM_VALUES = 6,
    OGL_TMON_LEVEL_DEFAULT = OGL_TMON_LEVEL_MOST
};

enum EValues_OGL_TRIPLE_BUFFER {
    OGL_TRIPLE_BUFFER_DISABLED                           = 0x00000000,
    OGL_TRIPLE_BUFFER_ENABLED                            = 0x00000001,
    OGL_TRIPLE_BUFFER_NUM_VALUES = 2,
    OGL_TRIPLE_BUFFER_DEFAULT = OGL_TRIPLE_BUFFER_DISABLED
};

enum EValues_AA_BEHAVIOR_FLAGS {
    AA_BEHAVIOR_FLAGS_NONE                               = 0x00000000,
    AA_BEHAVIOR_FLAGS_TREAT_OVERRIDE_AS_APP_CONTROLLED   = 0x00000001,
    AA_BEHAVIOR_FLAGS_TREAT_OVERRIDE_AS_ENHANCE          = 0x00000002,
    AA_BEHAVIOR_FLAGS_DISABLE_OVERRIDE                   = 0x00000003,
    AA_BEHAVIOR_FLAGS_TREAT_ENHANCE_AS_APP_CONTROLLED    = 0x00000004,
    AA_BEHAVIOR_FLAGS_TREAT_ENHANCE_AS_OVERRIDE          = 0x00000008,
    AA_BEHAVIOR_FLAGS_DISABLE_ENHANCE                    = 0x0000000c,
    AA_BEHAVIOR_FLAGS_MAP_VCAA_TO_MULTISAMPLING          = 0x00010000,
    AA_BEHAVIOR_FLAGS_SLI_DISABLE_TRANSPARENCY_SUPERSAMPLING = 0x00020000,
    AA_BEHAVIOR_FLAGS_DISABLE_CPLAA                      = 0x00040000,
    AA_BEHAVIOR_FLAGS_SKIP_RT_DIM_CHECK_FOR_ENHANCE      = 0x00080000,
    AA_BEHAVIOR_FLAGS_DISABLE_SLIAA                      = 0x00100000,
    AA_BEHAVIOR_FLAGS_DEFAULT                            = 0x00000000,
    AA_BEHAVIOR_FLAGS_AA_RT_BPP_DIV_4                    = 0xf0000000,
    AA_BEHAVIOR_FLAGS_AA_RT_BPP_DIV_4_SHIFT              = 28,
    AA_BEHAVIOR_FLAGS_NON_AA_RT_BPP_DIV_4                = 0x0f000000,
    AA_BEHAVIOR_FLAGS_NON_AA_RT_BPP_DIV_4_SHIFT          = 24,
    AA_BEHAVIOR_FLAGS_MASK                               = 0xff1f000f,
    AA_BEHAVIOR_FLAGS_NUM_VALUES = 18,
};

enum EValues_AA_MODE_ALPHATOCOVERAGE {
    AA_MODE_ALPHATOCOVERAGE_MODE_MASK                    = 0x00000004,
    AA_MODE_ALPHATOCOVERAGE_MODE_OFF                     = 0x00000000,
    AA_MODE_ALPHATOCOVERAGE_MODE_ON                      = 0x00000004,
    AA_MODE_ALPHATOCOVERAGE_MODE_MAX                     = 0x00000004,
    AA_MODE_ALPHATOCOVERAGE_NUM_VALUES = 4,
    AA_MODE_ALPHATOCOVERAGE_DEFAULT = 0x00000000
};

enum EValues_AA_MODE_GAMMACORRECTION {
    AA_MODE_GAMMACORRECTION_MASK                         = 0x00000003,
    AA_MODE_GAMMACORRECTION_OFF                          = 0x00000000,
    AA_MODE_GAMMACORRECTION_ON_IF_FOS                    = 0x00000001,
    AA_MODE_GAMMACORRECTION_ON_ALWAYS                    = 0x00000002,
    AA_MODE_GAMMACORRECTION_MAX                          = 0x00000002,
    AA_MODE_GAMMACORRECTION_DEFAULT                      = 0x00000000,
    AA_MODE_GAMMACORRECTION_DEFAULT_TESLA                = 0x00000002,
    AA_MODE_GAMMACORRECTION_DEFAULT_FERMI                = 0x00000002,
    AA_MODE_GAMMACORRECTION_NUM_VALUES = 8,
};

enum EValues_AA_MODE_METHOD {
    AA_MODE_METHOD_NONE                                  = 0x0,
    AA_MODE_METHOD_SUPERSAMPLE_2X_H                      = 0x1,
    AA_MODE_METHOD_SUPERSAMPLE_2X_V                      = 0x2,
    AA_MODE_METHOD_SUPERSAMPLE_1_5X1_5                   = 0x2,
    AA_MODE_METHOD_FREE_0x03                             = 0x3,
    AA_MODE_METHOD_FREE_0x04                             = 0x4,
    AA_MODE_METHOD_SUPERSAMPLE_4X                        = 0x5,
    AA_MODE_METHOD_SUPERSAMPLE_4X_BIAS                   = 0x6,
    AA_MODE_METHOD_SUPERSAMPLE_4X_GAUSSIAN               = 0x7,
    AA_MODE_METHOD_FREE_0x08                             = 0x8,
    AA_MODE_METHOD_FREE_0x09                             = 0x9,
    AA_MODE_METHOD_SUPERSAMPLE_9X                        = 0xA,
    AA_MODE_METHOD_SUPERSAMPLE_9X_BIAS                   = 0xB,
    AA_MODE_METHOD_SUPERSAMPLE_16X                       = 0xC,
    AA_MODE_METHOD_SUPERSAMPLE_16X_BIAS                  = 0xD,
    AA_MODE_METHOD_MULTISAMPLE_2X_DIAGONAL               = 0xE,
    AA_MODE_METHOD_MULTISAMPLE_2X_QUINCUNX               = 0xF,
    AA_MODE_METHOD_MULTISAMPLE_4X                        = 0x10,
    AA_MODE_METHOD_FREE_0x11                             = 0x11,
    AA_MODE_METHOD_MULTISAMPLE_4X_GAUSSIAN               = 0x12,
    AA_MODE_METHOD_MIXEDSAMPLE_4X_SKEWED_4TAP            = 0x13,
    AA_MODE_METHOD_FREE_0x14                             = 0x14,
    AA_MODE_METHOD_FREE_0x15                             = 0x15,
    AA_MODE_METHOD_MIXEDSAMPLE_6X                        = 0x16,
    AA_MODE_METHOD_MIXEDSAMPLE_6X_SKEWED_6TAP            = 0x17,
    AA_MODE_METHOD_MIXEDSAMPLE_8X                        = 0x18,
    AA_MODE_METHOD_MIXEDSAMPLE_8X_SKEWED_8TAP            = 0x19,
    AA_MODE_METHOD_MIXEDSAMPLE_16X                       = 0x1a,
    AA_MODE_METHOD_MULTISAMPLE_4X_GAMMA                  = 0x1b,
    AA_MODE_METHOD_MULTISAMPLE_16X                       = 0x1c,
    AA_MODE_METHOD_VCAA_32X_8v24                         = 0x1d,
    AA_MODE_METHOD_CORRUPTION_CHECK                      = 0x1e,
    AA_MODE_METHOD_6X_CT                                 = 0x1f,
    AA_MODE_METHOD_MULTISAMPLE_2X_DIAGONAL_GAMMA         = 0x20,
    AA_MODE_METHOD_SUPERSAMPLE_4X_GAMMA                  = 0x21,
    AA_MODE_METHOD_MULTISAMPLE_4X_FOSGAMMA               = 0x22,
    AA_MODE_METHOD_MULTISAMPLE_2X_DIAGONAL_FOSGAMMA      = 0x23,
    AA_MODE_METHOD_SUPERSAMPLE_4X_FOSGAMMA               = 0x24,
    AA_MODE_METHOD_MULTISAMPLE_8X                        = 0x25,
    AA_MODE_METHOD_VCAA_8X_4v4                           = 0x26,
    AA_MODE_METHOD_VCAA_16X_4v12                         = 0x27,
    AA_MODE_METHOD_VCAA_16X_8v8                          = 0x28,
    AA_MODE_METHOD_MIXEDSAMPLE_32X                       = 0x29,
    AA_MODE_METHOD_SUPERVCAA_64X_4v12                    = 0x2a,
    AA_MODE_METHOD_SUPERVCAA_64X_8v8                     = 0x2b,
    AA_MODE_METHOD_MIXEDSAMPLE_64X                       = 0x2c,
    AA_MODE_METHOD_MIXEDSAMPLE_128X                      = 0x2d,
    AA_MODE_METHOD_COUNT                                 = 0x2e,
    AA_MODE_METHOD_METHOD_MASK                           = 0x0000ffff,
    AA_MODE_METHOD_METHOD_MAX                            = 0xf1c57815,
    AA_MODE_METHOD_NUM_VALUES = 50,
    AA_MODE_METHOD_DEFAULT = AA_MODE_METHOD_NONE
};

enum EValues_AA_MODE_REPLAY {
    AA_MODE_REPLAY_SAMPLES_MASK                          = 0x00000070,
    AA_MODE_REPLAY_SAMPLES_ONE                           = 0x00000000,
    AA_MODE_REPLAY_SAMPLES_TWO                           = 0x00000010,
    AA_MODE_REPLAY_SAMPLES_FOUR                          = 0x00000020,
    AA_MODE_REPLAY_SAMPLES_EIGHT                         = 0x00000030,
    AA_MODE_REPLAY_SAMPLES_MAX                           = 0x00000030,
    AA_MODE_REPLAY_MODE_MASK                             = 0x0000000f,
    AA_MODE_REPLAY_MODE_OFF                              = 0x00000000,
    AA_MODE_REPLAY_MODE_ALPHA_TEST                       = 0x00000001,
    AA_MODE_REPLAY_MODE_PIXEL_KILL                       = 0x00000002,
    AA_MODE_REPLAY_MODE_DYN_BRANCH                       = 0x00000004,
    AA_MODE_REPLAY_MODE_OPTIMAL                          = 0x00000004,
    AA_MODE_REPLAY_MODE_ALL                              = 0x00000008,
    AA_MODE_REPLAY_MODE_MAX                              = 0x0000000f,
    AA_MODE_REPLAY_TRANSPARENCY                          = 0x00000023,
    AA_MODE_REPLAY_DISALLOW_TRAA                         = 0x00000100,
    AA_MODE_REPLAY_TRANSPARENCY_DEFAULT                  = 0x00000000,
    AA_MODE_REPLAY_TRANSPARENCY_DEFAULT_TESLA            = 0x00000000,
    AA_MODE_REPLAY_TRANSPARENCY_DEFAULT_FERMI            = 0x00000000,
    AA_MODE_REPLAY_MASK                                  = 0x0000017f,
    AA_MODE_REPLAY_NUM_VALUES = 20,
    AA_MODE_REPLAY_DEFAULT = 0x00000000
};

enum EValues_AA_MODE_SELECTOR {
    AA_MODE_SELECTOR_MASK                                = 0x00000003,
    AA_MODE_SELECTOR_APP_CONTROL                         = 0x00000000,
    AA_MODE_SELECTOR_OVERRIDE                            = 0x00000001,
    AA_MODE_SELECTOR_ENHANCE                             = 0x00000002,
    AA_MODE_SELECTOR_MAX                                 = 0x00000002,
    AA_MODE_SELECTOR_NUM_VALUES = 5,
    AA_MODE_SELECTOR_DEFAULT = AA_MODE_SELECTOR_APP_CONTROL
};

enum EValues_AA_MODE_SELECTOR_SLIAA {
    AA_MODE_SELECTOR_SLIAA_DISABLED                      = 0,
    AA_MODE_SELECTOR_SLIAA_ENABLED                       = 1,
    AA_MODE_SELECTOR_SLIAA_NUM_VALUES = 2,
    AA_MODE_SELECTOR_SLIAA_DEFAULT = AA_MODE_SELECTOR_SLIAA_DISABLED
};

enum EValues_ANISO_MODE_LEVEL {
    ANISO_MODE_LEVEL_MASK                                = 0x0000ffff,
    ANISO_MODE_LEVEL_NONE_POINT                          = 0x00000000,
    ANISO_MODE_LEVEL_NONE_LINEAR                         = 0x00000001,
    ANISO_MODE_LEVEL_MAX                                 = 0x00000010,
    ANISO_MODE_LEVEL_DEFAULT                             = 0x00000001,
    ANISO_MODE_LEVEL_NUM_VALUES = 5,
};

enum EValues_ANISO_MODE_SELECTOR {
    ANISO_MODE_SELECTOR_MASK                             = 0x0000000f,
    ANISO_MODE_SELECTOR_APP                              = 0x00000000,
    ANISO_MODE_SELECTOR_USER                             = 0x00000001,
    ANISO_MODE_SELECTOR_COND                             = 0x00000002,
    ANISO_MODE_SELECTOR_MAX                              = 0x00000002,
    ANISO_MODE_SELECTOR_DEFAULT                          = 0x00000000,
    ANISO_MODE_SELECTOR_NUM_VALUES = 6,
};

enum EValues_ANSEL_ALLOW {
    ANSEL_ALLOW_DISALLOWED                               = 0,
    ANSEL_ALLOW_ALLOWED                                  = 1,
    ANSEL_ALLOW_NUM_VALUES = 2,
    ANSEL_ALLOW_DEFAULT = ANSEL_ALLOW_ALLOWED
};

enum EValues_ANSEL_ALLOWLISTED {
    ANSEL_ALLOWLISTED_DISALLOWED                         = 0,
    ANSEL_ALLOWLISTED_ALLOWED                            = 1,
    ANSEL_ALLOWLISTED_NUM_VALUES = 2,
    ANSEL_ALLOWLISTED_DEFAULT = ANSEL_ALLOWLISTED_DISALLOWED
};

enum EValues_ANSEL_ENABLE {
    ANSEL_ENABLE_OFF                                     = 0,
    ANSEL_ENABLE_ON                                      = 1,
    ANSEL_ENABLE_NUM_VALUES = 2,
    ANSEL_ENABLE_DEFAULT = ANSEL_ENABLE_ON
};

enum EValues_APPLICATION_PROFILE_NOTIFICATION_TIMEOUT {
    APPLICATION_PROFILE_NOTIFICATION_TIMEOUT_DISABLED    = 0,
    APPLICATION_PROFILE_NOTIFICATION_TIMEOUT_NINE_SECONDS = 9,
    APPLICATION_PROFILE_NOTIFICATION_TIMEOUT_FIFTEEN_SECONDS = 15,
    APPLICATION_PROFILE_NOTIFICATION_TIMEOUT_THIRTY_SECONDS = 30,
    APPLICATION_PROFILE_NOTIFICATION_TIMEOUT_ONE_MINUTE  = 60,
    APPLICATION_PROFILE_NOTIFICATION_TIMEOUT_TWO_MINUTES = 120,
    APPLICATION_PROFILE_NOTIFICATION_TIMEOUT_NUM_VALUES = 6,
    APPLICATION_PROFILE_NOTIFICATION_TIMEOUT_DEFAULT = APPLICATION_PROFILE_NOTIFICATION_TIMEOUT_DISABLED
};

enum EValues_BATTERY_BOOST_APP_FPS {
    BATTERY_BOOST_APP_FPS_MIN                            = 0x00000001,
    BATTERY_BOOST_APP_FPS_MAX                            = 0x000003ff,
    BATTERY_BOOST_APP_FPS_NO_OVERRIDE                    = 0x00000000,
    BATTERY_BOOST_APP_FPS_NUM_VALUES = 3,
    BATTERY_BOOST_APP_FPS_DEFAULT = BATTERY_BOOST_APP_FPS_NO_OVERRIDE
};

enum EValues_CPL_HIDDEN_PROFILE {
    CPL_HIDDEN_PROFILE_DISABLED                          = 0,
    CPL_HIDDEN_PROFILE_ENABLED                           = 1,
    CPL_HIDDEN_PROFILE_NUM_VALUES = 2,
    CPL_HIDDEN_PROFILE_DEFAULT = CPL_HIDDEN_PROFILE_DISABLED
};

#define    CUDA_EXCLUDED_GPUS_NONE                              L"none"
#define    CUDA_EXCLUDED_GPUS_NUM_VALUES 1
#define    CUDA_EXCLUDED_GPUS_DEFAULT CUDA_EXCLUDED_GPUS_NONE

#define    D3DOGL_GPU_MAX_POWER_DEFAULTPOWER                    L"0"
#define    D3DOGL_GPU_MAX_POWER_NUM_VALUES 1
#define    D3DOGL_GPU_MAX_POWER_DEFAULT D3DOGL_GPU_MAX_POWER_DEFAULTPOWER

enum EValues_EXPORT_PERF_COUNTERS {
    EXPORT_PERF_COUNTERS_OFF                             = 0x00000000,
    EXPORT_PERF_COUNTERS_ON                              = 0x00000001,
    EXPORT_PERF_COUNTERS_NUM_VALUES = 2,
    EXPORT_PERF_COUNTERS_DEFAULT = EXPORT_PERF_COUNTERS_OFF
};

enum EValues_EXTERNAL_QUIET_MODE {
    EXTERNAL_QUIET_MODE_ON                               = 0x00000001,
    EXTERNAL_QUIET_MODE_OFF                              = 0x00000000,
    EXTERNAL_QUIET_MODE_NUM_VALUES = 2,
    EXTERNAL_QUIET_MODE_DEFAULT = EXTERNAL_QUIET_MODE_OFF
};

enum EValues_FRL_FPS {
    FRL_FPS_DISABLED                                     = 0x00000000,
    FRL_FPS_MIN                                          = 0x00000000,
    FRL_FPS_MAX                                          = 0x000003ff,
    FRL_FPS_NUM_VALUES = 3,
    FRL_FPS_DEFAULT = FRL_FPS_DISABLED
};

enum EValues_FXAA_ALLOW {
    FXAA_ALLOW_DISALLOWED                                = 0,
    FXAA_ALLOW_ALLOWED                                   = 1,
    FXAA_ALLOW_NUM_VALUES = 2,
    FXAA_ALLOW_DEFAULT = FXAA_ALLOW_ALLOWED
};

enum EValues_FXAA_ENABLE {
    FXAA_ENABLE_OFF                                      = 0,
    FXAA_ENABLE_ON                                       = 1,
    FXAA_ENABLE_NUM_VALUES = 2,
    FXAA_ENABLE_DEFAULT = FXAA_ENABLE_OFF
};

enum EValues_FXAA_INDICATOR_ENABLE {
    FXAA_INDICATOR_ENABLE_OFF                            = 0,
    FXAA_INDICATOR_ENABLE_ON                             = 1,
    FXAA_INDICATOR_ENABLE_NUM_VALUES = 2,
    FXAA_INDICATOR_ENABLE_DEFAULT = FXAA_INDICATOR_ENABLE_OFF
};

enum EValues_LATENCY_INDICATOR_AUTOALIGN {
    LATENCY_INDICATOR_AUTOALIGN_DISABLED                 = 0x0,
    LATENCY_INDICATOR_AUTOALIGN_ENABLED                  = 0x1,
    LATENCY_INDICATOR_AUTOALIGN_NUM_VALUES = 2,
    LATENCY_INDICATOR_AUTOALIGN_DEFAULT = LATENCY_INDICATOR_AUTOALIGN_ENABLED
};

enum EValues_MCSFRSHOWSPLIT {
    MCSFRSHOWSPLIT_DISABLED                              = 0x34534064,
    MCSFRSHOWSPLIT_ENABLED                               = 0x24545582,
    MCSFRSHOWSPLIT_NUM_VALUES = 2,
    MCSFRSHOWSPLIT_DEFAULT = MCSFRSHOWSPLIT_DISABLED
};

enum EValues_NV_QUALITY_UPSCALING {
    NV_QUALITY_UPSCALING_OFF                             = 0,
    NV_QUALITY_UPSCALING_ON                              = 1,
    NV_QUALITY_UPSCALING_NUM_VALUES = 2,
    NV_QUALITY_UPSCALING_DEFAULT = NV_QUALITY_UPSCALING_OFF
};

enum EValues_OPTIMUS_MAXAA {
    OPTIMUS_MAXAA_MIN                                    = 0,
    OPTIMUS_MAXAA_MAX                                    = 16,
    OPTIMUS_MAXAA_NUM_VALUES = 2,
    OPTIMUS_MAXAA_DEFAULT = 0
};

enum EValues_PHYSXINDICATOR {
    PHYSXINDICATOR_DISABLED                              = 0x34534064,
    PHYSXINDICATOR_ENABLED                               = 0x24545582,
    PHYSXINDICATOR_NUM_VALUES = 2,
    PHYSXINDICATOR_DEFAULT = PHYSXINDICATOR_DISABLED
};

enum EValues_PREFERRED_PSTATE {
    PREFERRED_PSTATE_ADAPTIVE                            = 0x00000000,
    PREFERRED_PSTATE_PREFER_MAX                          = 0x00000001,
    PREFERRED_PSTATE_DRIVER_CONTROLLED                   = 0x00000002,
    PREFERRED_PSTATE_PREFER_CONSISTENT_PERFORMANCE       = 0x00000003,
    PREFERRED_PSTATE_PREFER_MIN                          = 0x00000004,
    PREFERRED_PSTATE_OPTIMAL_POWER                       = 0x00000005,
    PREFERRED_PSTATE_MIN                                 = 0x00000000,
    PREFERRED_PSTATE_MAX                                 = 0x00000005,
    PREFERRED_PSTATE_NUM_VALUES = 8,
    PREFERRED_PSTATE_DEFAULT = PREFERRED_PSTATE_OPTIMAL_POWER
};

enum EValues_PREVENT_UI_AF_OVERRIDE {
    PREVENT_UI_AF_OVERRIDE_OFF                           = 0,
    PREVENT_UI_AF_OVERRIDE_ON                            = 1,
    PREVENT_UI_AF_OVERRIDE_NUM_VALUES = 2,
    PREVENT_UI_AF_OVERRIDE_DEFAULT = PREVENT_UI_AF_OVERRIDE_OFF
};

enum EValues_SHIM_MCCOMPAT {
    SHIM_MCCOMPAT_INTEGRATED                             = 0x00000000U,
    SHIM_MCCOMPAT_ENABLE                                 = 0x00000001U,
    SHIM_MCCOMPAT_USER_EDITABLE                          = 0x00000002U,
    SHIM_MCCOMPAT_MASK                                   = 0x00000003U,
    SHIM_MCCOMPAT_VIDEO_MASK                             = 0x00000004U,
    SHIM_MCCOMPAT_VARYING_BIT                            = 0x00000008U,
    SHIM_MCCOMPAT_AUTO_SELECT                            = 0x00000010U,
    SHIM_MCCOMPAT_OVERRIDE_BIT                           = 0x80000000U,
    SHIM_MCCOMPAT_NUM_VALUES = 8,
    SHIM_MCCOMPAT_DEFAULT = SHIM_MCCOMPAT_AUTO_SELECT
};

enum EValues_SHIM_RENDERING_MODE {
    SHIM_RENDERING_MODE_INTEGRATED                       = 0x00000000U,
    SHIM_RENDERING_MODE_ENABLE                           = 0x00000001U,
    SHIM_RENDERING_MODE_USER_EDITABLE                    = 0x00000002U,
    SHIM_RENDERING_MODE_MASK                             = 0x00000003U,
    SHIM_RENDERING_MODE_VIDEO_MASK                       = 0x00000004U,
    SHIM_RENDERING_MODE_VARYING_BIT                      = 0x00000008U,
    SHIM_RENDERING_MODE_AUTO_SELECT                      = 0x00000010U,
    SHIM_RENDERING_MODE_OVERRIDE_BIT                     = 0x80000000U,
    SHIM_RENDERING_MODE_NUM_VALUES = 8,
    SHIM_RENDERING_MODE_DEFAULT = SHIM_RENDERING_MODE_AUTO_SELECT
};

enum EValues_SHIM_RENDERING_OPTIONS {
    SHIM_RENDERING_OPTIONS_DEFAULT_RENDERING_MODE        = 0x00000000U,
    SHIM_RENDERING_OPTIONS_DISABLE_ASYNC_PRESENT         = 0x00000001U,
    SHIM_RENDERING_OPTIONS_EHSHELL_DETECT                = 0x00000002U,
    SHIM_RENDERING_OPTIONS_FLASHPLAYER_HOST_DETECT       = 0x00000004U,
    SHIM_RENDERING_OPTIONS_VIDEO_DRM_APP_DETECT          = 0x00000008U,
    SHIM_RENDERING_OPTIONS_IGNORE_OVERRIDES              = 0x00000010U,
    SHIM_RENDERING_OPTIONS_RESERVED1                     = 0x00000020U,
    SHIM_RENDERING_OPTIONS_ENABLE_DWM_ASYNC_PRESENT      = 0x00000040U,
    SHIM_RENDERING_OPTIONS_RESERVED2                     = 0x00000080U,
    SHIM_RENDERING_OPTIONS_ALLOW_INHERITANCE             = 0x00000100U,
    SHIM_RENDERING_OPTIONS_DISABLE_WRAPPERS              = 0x00000200U,
    SHIM_RENDERING_OPTIONS_DISABLE_DXGI_WRAPPERS         = 0x00000400U,
    SHIM_RENDERING_OPTIONS_PRUNE_UNSUPPORTED_FORMATS     = 0x00000800U,
    SHIM_RENDERING_OPTIONS_ENABLE_ALPHA_FORMAT           = 0x00001000U,
    SHIM_RENDERING_OPTIONS_IGPU_TRANSCODING              = 0x00002000U,
    SHIM_RENDERING_OPTIONS_DISABLE_CUDA                  = 0x00004000U,
    SHIM_RENDERING_OPTIONS_ALLOW_CP_CAPS_FOR_VIDEO       = 0x00008000U,
    SHIM_RENDERING_OPTIONS_IGPU_TRANSCODING_FWD_OPTIMUS  = 0x00010000U,
    SHIM_RENDERING_OPTIONS_DISABLE_DURING_SECURE_BOOT    = 0x00020000U,
    SHIM_RENDERING_OPTIONS_INVERT_FOR_QUADRO             = 0x00040000U,
    SHIM_RENDERING_OPTIONS_INVERT_FOR_MSHYBRID           = 0x00080000U,
    SHIM_RENDERING_OPTIONS_REGISTER_PROCESS_ENABLE_GOLD  = 0x00100000U,
    SHIM_RENDERING_OPTIONS_HANDLE_WINDOWED_MODE_PERF_OPT = 0x00200000U,
    SHIM_RENDERING_OPTIONS_HANDLE_WIN7_ASYNC_RUNTIME_BUG = 0x00400000U,
    SHIM_RENDERING_OPTIONS_EXPLICIT_ADAPTER_OPTED_BY_APP = 0x00800000U,
    SHIM_RENDERING_OPTIONS_ALLOW_DYNAMIC_DISPLAY_MUX_SWITCH = 0x01000000U,
    SHIM_RENDERING_OPTIONS_DISALLOW_DYNAMIC_DISPLAY_MUX_SWITCH = 0x02000000U,
    SHIM_RENDERING_OPTIONS_DISABLE_TURING_POWER_POLICY   = 0x04000000U,
    SHIM_RENDERING_OPTIONS_NUM_VALUES = 28,
    SHIM_RENDERING_OPTIONS_DEFAULT = 0x00000000U
};

enum EValues_SLI_GPU_COUNT {
    SLI_GPU_COUNT_AUTOSELECT                             = 0x00000000,
    SLI_GPU_COUNT_ONE                                    = 0x00000001,
    SLI_GPU_COUNT_TWO                                    = 0x00000002,
    SLI_GPU_COUNT_THREE                                  = 0x00000003,
    SLI_GPU_COUNT_FOUR                                   = 0x00000004,
    SLI_GPU_COUNT_NUM_VALUES = 5,
    SLI_GPU_COUNT_DEFAULT = SLI_GPU_COUNT_AUTOSELECT
};

enum EValues_SLI_PREDEFINED_GPU_COUNT {
    SLI_PREDEFINED_GPU_COUNT_AUTOSELECT                  = 0x00000000,
    SLI_PREDEFINED_GPU_COUNT_ONE                         = 0x00000001,
    SLI_PREDEFINED_GPU_COUNT_TWO                         = 0x00000002,
    SLI_PREDEFINED_GPU_COUNT_THREE                       = 0x00000003,
    SLI_PREDEFINED_GPU_COUNT_FOUR                        = 0x00000004,
    SLI_PREDEFINED_GPU_COUNT_NUM_VALUES = 5,
    SLI_PREDEFINED_GPU_COUNT_DEFAULT = SLI_PREDEFINED_GPU_COUNT_AUTOSELECT
};

enum EValues_SLI_PREDEFINED_GPU_COUNT_DX10 {
    SLI_PREDEFINED_GPU_COUNT_DX10_AUTOSELECT             = 0x00000000,
    SLI_PREDEFINED_GPU_COUNT_DX10_ONE                    = 0x00000001,
    SLI_PREDEFINED_GPU_COUNT_DX10_TWO                    = 0x00000002,
    SLI_PREDEFINED_GPU_COUNT_DX10_THREE                  = 0x00000003,
    SLI_PREDEFINED_GPU_COUNT_DX10_FOUR                   = 0x00000004,
    SLI_PREDEFINED_GPU_COUNT_DX10_NUM_VALUES = 5,
    SLI_PREDEFINED_GPU_COUNT_DX10_DEFAULT = SLI_PREDEFINED_GPU_COUNT_DX10_AUTOSELECT
};

enum EValues_SLI_PREDEFINED_MODE {
    SLI_PREDEFINED_MODE_AUTOSELECT                       = 0x00000000,
    SLI_PREDEFINED_MODE_FORCE_SINGLE                     = 0x00000001,
    SLI_PREDEFINED_MODE_FORCE_AFR                        = 0x00000002,
    SLI_PREDEFINED_MODE_FORCE_AFR2                       = 0x00000003,
    SLI_PREDEFINED_MODE_FORCE_SFR                        = 0x00000004,
    SLI_PREDEFINED_MODE_FORCE_AFR_OF_SFR__FALLBACK_3AFR  = 0x00000005,
    SLI_PREDEFINED_MODE_NUM_VALUES = 6,
    SLI_PREDEFINED_MODE_DEFAULT = SLI_PREDEFINED_MODE_AUTOSELECT
};

enum EValues_SLI_PREDEFINED_MODE_DX10 {
    SLI_PREDEFINED_MODE_DX10_AUTOSELECT                  = 0x00000000,
    SLI_PREDEFINED_MODE_DX10_FORCE_SINGLE                = 0x00000001,
    SLI_PREDEFINED_MODE_DX10_FORCE_AFR                   = 0x00000002,
    SLI_PREDEFINED_MODE_DX10_FORCE_AFR2                  = 0x00000003,
    SLI_PREDEFINED_MODE_DX10_FORCE_SFR                   = 0x00000004,
    SLI_PREDEFINED_MODE_DX10_FORCE_AFR_OF_SFR__FALLBACK_3AFR = 0x00000005,
    SLI_PREDEFINED_MODE_DX10_NUM_VALUES = 6,
    SLI_PREDEFINED_MODE_DX10_DEFAULT = SLI_PREDEFINED_MODE_DX10_AUTOSELECT
};

enum EValues_SLI_RENDERING_MODE {
    SLI_RENDERING_MODE_AUTOSELECT                        = 0x00000000,
    SLI_RENDERING_MODE_FORCE_SINGLE                      = 0x00000001,
    SLI_RENDERING_MODE_FORCE_AFR                         = 0x00000002,
    SLI_RENDERING_MODE_FORCE_AFR2                        = 0x00000003,
    SLI_RENDERING_MODE_FORCE_SFR                         = 0x00000004,
    SLI_RENDERING_MODE_FORCE_AFR_OF_SFR__FALLBACK_3AFR   = 0x00000005,
    SLI_RENDERING_MODE_NUM_VALUES = 6,
    SLI_RENDERING_MODE_DEFAULT = SLI_RENDERING_MODE_AUTOSELECT
};

enum EValues_VRPRERENDERLIMIT {
    VRPRERENDERLIMIT_MIN                                 = 0x00,
    VRPRERENDERLIMIT_MAX                                 = 0xff,
    VRPRERENDERLIMIT_APP_CONTROLLED                      = 0x00,
    VRPRERENDERLIMIT_DEFAULT                             = 0x01,
    VRPRERENDERLIMIT_NUM_VALUES = 4,
};

enum EValues_VRRFEATUREINDICATOR {
    VRRFEATUREINDICATOR_DISABLED                         = 0x0,
    VRRFEATUREINDICATOR_ENABLED                          = 0x1,
    VRRFEATUREINDICATOR_NUM_VALUES = 2,
    VRRFEATUREINDICATOR_DEFAULT = VRRFEATUREINDICATOR_ENABLED
};

enum EValues_VRROVERLAYINDICATOR {
    VRROVERLAYINDICATOR_DISABLED                         = 0x0,
    VRROVERLAYINDICATOR_ENABLED                          = 0x1,
    VRROVERLAYINDICATOR_NUM_VALUES = 2,
    VRROVERLAYINDICATOR_DEFAULT = VRROVERLAYINDICATOR_ENABLED
};

enum EValues_VRRREQUESTSTATE {
    VRRREQUESTSTATE_DISABLED                             = 0x0,
    VRRREQUESTSTATE_FULLSCREEN_ONLY                      = 0x1,
    VRRREQUESTSTATE_FULLSCREEN_AND_WINDOWED              = 0x2,
    VRRREQUESTSTATE_NUM_VALUES = 3,
    VRRREQUESTSTATE_DEFAULT = VRRREQUESTSTATE_FULLSCREEN_ONLY
};

enum EValues_VRR_APP_OVERRIDE {
    VRR_APP_OVERRIDE_ALLOW                               = 0,
    VRR_APP_OVERRIDE_FORCE_OFF                           = 1,
    VRR_APP_OVERRIDE_DISALLOW                            = 2,
    VRR_APP_OVERRIDE_ULMB                                = 3,
    VRR_APP_OVERRIDE_FIXED_REFRESH                       = 4,
    VRR_APP_OVERRIDE_NUM_VALUES = 5,
    VRR_APP_OVERRIDE_DEFAULT = VRR_APP_OVERRIDE_ALLOW
};

enum EValues_VRR_APP_OVERRIDE_REQUEST_STATE {
    VRR_APP_OVERRIDE_REQUEST_STATE_ALLOW                 = 0,
    VRR_APP_OVERRIDE_REQUEST_STATE_FORCE_OFF             = 1,
    VRR_APP_OVERRIDE_REQUEST_STATE_DISALLOW              = 2,
    VRR_APP_OVERRIDE_REQUEST_STATE_ULMB                  = 3,
    VRR_APP_OVERRIDE_REQUEST_STATE_FIXED_REFRESH         = 4,
    VRR_APP_OVERRIDE_REQUEST_STATE_NUM_VALUES = 5,
    VRR_APP_OVERRIDE_REQUEST_STATE_DEFAULT = VRR_APP_OVERRIDE_REQUEST_STATE_ALLOW
};

enum EValues_VRR_MODE {
    VRR_MODE_DISABLED                                    = 0x0,
    VRR_MODE_FULLSCREEN_ONLY                             = 0x1,
    VRR_MODE_FULLSCREEN_AND_WINDOWED                     = 0x2,
    VRR_MODE_NUM_VALUES = 3,
    VRR_MODE_DEFAULT = VRR_MODE_FULLSCREEN_ONLY
};

enum EValues_VSYNCSMOOTHAFR {
    VSYNCSMOOTHAFR_OFF                                   = 0x00000000,
    VSYNCSMOOTHAFR_ON                                    = 0x00000001,
    VSYNCSMOOTHAFR_NUM_VALUES = 2,
    VSYNCSMOOTHAFR_DEFAULT = VSYNCSMOOTHAFR_OFF
};

enum EValues_VSYNCVRRCONTROL {
    VSYNCVRRCONTROL_DISABLE                              = 0x00000000,
    VSYNCVRRCONTROL_ENABLE                               = 0x00000001,
    VSYNCVRRCONTROL_NOTSUPPORTED                         = 0x9f95128e,
    VSYNCVRRCONTROL_NUM_VALUES = 3,
    VSYNCVRRCONTROL_DEFAULT = VSYNCVRRCONTROL_ENABLE
};

enum EValues_VSYNC_BEHAVIOR_FLAGS {
    VSYNC_BEHAVIOR_FLAGS_NONE                            = 0x00000000,
    VSYNC_BEHAVIOR_FLAGS_DEFAULT                         = 0x00000000,
    VSYNC_BEHAVIOR_FLAGS_IGNORE_FLIPINTERVAL_MULTIPLE    = 0x00000001,
    VSYNC_BEHAVIOR_FLAGS_NUM_VALUES = 3,
};

enum EValues_WKS_API_STEREO_EYES_EXCHANGE {
    WKS_API_STEREO_EYES_EXCHANGE_OFF                     = 0,
    WKS_API_STEREO_EYES_EXCHANGE_ON                      = 1,
    WKS_API_STEREO_EYES_EXCHANGE_NUM_VALUES = 2,
    WKS_API_STEREO_EYES_EXCHANGE_DEFAULT = WKS_API_STEREO_EYES_EXCHANGE_OFF
};

enum EValues_WKS_API_STEREO_MODE {
    WKS_API_STEREO_MODE_SHUTTER_GLASSES                  = 0,
    WKS_API_STEREO_MODE_VERTICAL_INTERLACED              = 1,
    WKS_API_STEREO_MODE_TWINVIEW                         = 2,
    WKS_API_STEREO_MODE_NV17_SHUTTER_GLASSES_AUTO        = 3,
    WKS_API_STEREO_MODE_NV17_SHUTTER_GLASSES_DAC0        = 4,
    WKS_API_STEREO_MODE_NV17_SHUTTER_GLASSES_DAC1        = 5,
    WKS_API_STEREO_MODE_COLOR_LINE                       = 6,
    WKS_API_STEREO_MODE_COLOR_INTERLEAVED                = 7,
    WKS_API_STEREO_MODE_ANAGLYPH                         = 8,
    WKS_API_STEREO_MODE_HORIZONTAL_INTERLACED            = 9,
    WKS_API_STEREO_MODE_SIDE_FIELD                       = 10,
    WKS_API_STEREO_MODE_SUB_FIELD                        = 11,
    WKS_API_STEREO_MODE_CHECKERBOARD                     = 12,
    WKS_API_STEREO_MODE_INVERSE_CHECKERBOARD             = 13,
    WKS_API_STEREO_MODE_TRIDELITY_SL                     = 14,
    WKS_API_STEREO_MODE_TRIDELITY_MV                     = 15,
    WKS_API_STEREO_MODE_SEEFRONT                         = 16,
    WKS_API_STEREO_MODE_STEREO_MIRROR                    = 17,
    WKS_API_STEREO_MODE_FRAME_SEQUENTIAL                 = 18,
    WKS_API_STEREO_MODE_AUTODETECT_PASSIVE_MODE          = 19,
    WKS_API_STEREO_MODE_AEGIS_DT_FRAME_SEQUENTIAL        = 20,
    WKS_API_STEREO_MODE_OEM_EMITTER_FRAME_SEQUENTIAL     = 21,
    WKS_API_STEREO_MODE_DP_INBAND                        = 22,
    WKS_API_STEREO_MODE_USE_HW_DEFAULT                   = 0xffffffff,
    WKS_API_STEREO_MODE_DEFAULT_GL                       = 3,
    WKS_API_STEREO_MODE_NUM_VALUES = 25,
    WKS_API_STEREO_MODE_DEFAULT = WKS_API_STEREO_MODE_SHUTTER_GLASSES
};

enum EValues_WKS_MEMORY_ALLOCATION_POLICY {
    WKS_MEMORY_ALLOCATION_POLICY_AS_NEEDED               = 0x0,
    WKS_MEMORY_ALLOCATION_POLICY_MODERATE_PRE_ALLOCATION = 0x1,
    WKS_MEMORY_ALLOCATION_POLICY_AGGRESSIVE_PRE_ALLOCATION = 0x2,
    WKS_MEMORY_ALLOCATION_POLICY_NUM_VALUES = 3,
    WKS_MEMORY_ALLOCATION_POLICY_DEFAULT = WKS_MEMORY_ALLOCATION_POLICY_AS_NEEDED
};

enum EValues_WKS_STEREO_DONGLE_SUPPORT {
    WKS_STEREO_DONGLE_SUPPORT_OFF                        = 0,
    WKS_STEREO_DONGLE_SUPPORT_DAC                        = 1,
    WKS_STEREO_DONGLE_SUPPORT_DLP                        = 2,
    WKS_STEREO_DONGLE_SUPPORT_NUM_VALUES = 3,
    WKS_STEREO_DONGLE_SUPPORT_DEFAULT = WKS_STEREO_DONGLE_SUPPORT_DAC
};

enum EValues_WKS_STEREO_SUPPORT {
    WKS_STEREO_SUPPORT_OFF                               = 0,
    WKS_STEREO_SUPPORT_ON                                = 1,
    WKS_STEREO_SUPPORT_NUM_VALUES = 2,
    WKS_STEREO_SUPPORT_DEFAULT = WKS_STEREO_SUPPORT_OFF
};

enum EValues_WKS_STEREO_SWAP_MODE {
    WKS_STEREO_SWAP_MODE_APPLICATION_CONTROL             = 0x0,
    WKS_STEREO_SWAP_MODE_PER_EYE                         = 0x1,
    WKS_STEREO_SWAP_MODE_PER_EYE_PAIR                    = 0x2,
    WKS_STEREO_SWAP_MODE_LEGACY_BEHAVIOR                 = 0x3,
    WKS_STEREO_SWAP_MODE_PER_EYE_FOR_SWAP_GROUP          = 0x4,
    WKS_STEREO_SWAP_MODE_NUM_VALUES = 5,
    WKS_STEREO_SWAP_MODE_DEFAULT = WKS_STEREO_SWAP_MODE_APPLICATION_CONTROL
};

enum EValues_AO_MODE {
    AO_MODE_OFF                                          = 0,
    AO_MODE_LOW                                          = 1,
    AO_MODE_MEDIUM                                       = 2,
    AO_MODE_HIGH                                         = 3,
    AO_MODE_NUM_VALUES = 4,
    AO_MODE_DEFAULT = AO_MODE_OFF
};

enum EValues_AO_MODE_ACTIVE {
    AO_MODE_ACTIVE_DISABLED                              = 0,
    AO_MODE_ACTIVE_ENABLED                               = 1,
    AO_MODE_ACTIVE_NUM_VALUES = 2,
    AO_MODE_ACTIVE_DEFAULT = AO_MODE_ACTIVE_DISABLED
};

enum EValues_AUTO_LODBIASADJUST {
    AUTO_LODBIASADJUST_OFF                               = 0x00000000,
    AUTO_LODBIASADJUST_ON                                = 0x00000001,
    AUTO_LODBIASADJUST_NUM_VALUES = 2,
    AUTO_LODBIASADJUST_DEFAULT = AUTO_LODBIASADJUST_ON
};

enum EValues_EXPORT_PERF_COUNTERS_DX9_ONLY {
    EXPORT_PERF_COUNTERS_DX9_ONLY_OFF                    = 0x00000000,
    EXPORT_PERF_COUNTERS_DX9_ONLY_ON                     = 0x00000001,
    EXPORT_PERF_COUNTERS_DX9_ONLY_NUM_VALUES = 2,
    EXPORT_PERF_COUNTERS_DX9_ONLY_DEFAULT = EXPORT_PERF_COUNTERS_DX9_ONLY_OFF
};

enum EValues_LODBIASADJUST {
    LODBIASADJUST_MIN                                    = 0xffffff80,
    LODBIASADJUST_MAX                                    = 128,
    LODBIASADJUST_NUM_VALUES = 2,
    LODBIASADJUST_DEFAULT = 0
};

enum EValues_MAXWELL_B_SAMPLE_INTERLEAVE {
    MAXWELL_B_SAMPLE_INTERLEAVE_OFF                      = 0,
    MAXWELL_B_SAMPLE_INTERLEAVE_ON                       = 1,
    MAXWELL_B_SAMPLE_INTERLEAVE_NUM_VALUES = 2,
    MAXWELL_B_SAMPLE_INTERLEAVE_DEFAULT = MAXWELL_B_SAMPLE_INTERLEAVE_OFF
};

enum EValues_PRERENDERLIMIT {
    PRERENDERLIMIT_MIN                                   = 0x00,
    PRERENDERLIMIT_MAX                                   = 0xff,
    PRERENDERLIMIT_APP_CONTROLLED                        = 0x00,
    PRERENDERLIMIT_NUM_VALUES = 3,
    PRERENDERLIMIT_DEFAULT = PRERENDERLIMIT_APP_CONTROLLED
};

enum EValues_PS_SHADERDISKCACHE {
    PS_SHADERDISKCACHE_OFF                               = 0x00000000,
    PS_SHADERDISKCACHE_ON                                = 0x00000001,
    PS_SHADERDISKCACHE_NUM_VALUES = 2,
    PS_SHADERDISKCACHE_DEFAULT = PS_SHADERDISKCACHE_ON
};

enum EValues_PS_SHADERDISKCACHE_MAX_SIZE {
    PS_SHADERDISKCACHE_MAX_SIZE_MIN                      = 0x0,
    PS_SHADERDISKCACHE_MAX_SIZE_MAX                      = 0xffffffff,
    PS_SHADERDISKCACHE_MAX_SIZE_NUM_VALUES = 2,
    PS_SHADERDISKCACHE_MAX_SIZE_DEFAULT = 0x1000
};

enum EValues_PS_TEXFILTER_ANISO_OPTS2 {
    PS_TEXFILTER_ANISO_OPTS2_OFF                         = 0x00000000,
    PS_TEXFILTER_ANISO_OPTS2_ON                          = 0x00000001,
    PS_TEXFILTER_ANISO_OPTS2_NUM_VALUES = 2,
    PS_TEXFILTER_ANISO_OPTS2_DEFAULT = PS_TEXFILTER_ANISO_OPTS2_OFF
};

enum EValues_PS_TEXFILTER_BILINEAR_IN_ANISO {
    PS_TEXFILTER_BILINEAR_IN_ANISO_OFF                   = 0x00000000,
    PS_TEXFILTER_BILINEAR_IN_ANISO_ON                    = 0x00000001,
    PS_TEXFILTER_BILINEAR_IN_ANISO_NUM_VALUES = 2,
    PS_TEXFILTER_BILINEAR_IN_ANISO_DEFAULT = PS_TEXFILTER_BILINEAR_IN_ANISO_OFF
};

enum EValues_PS_TEXFILTER_DISABLE_TRILIN_SLOPE {
    PS_TEXFILTER_DISABLE_TRILIN_SLOPE_OFF                = 0x00000000,
    PS_TEXFILTER_DISABLE_TRILIN_SLOPE_ON                 = 0x00000001,
    PS_TEXFILTER_DISABLE_TRILIN_SLOPE_NUM_VALUES = 2,
    PS_TEXFILTER_DISABLE_TRILIN_SLOPE_DEFAULT = PS_TEXFILTER_DISABLE_TRILIN_SLOPE_OFF
};

enum EValues_PS_TEXFILTER_NO_NEG_LODBIAS {
    PS_TEXFILTER_NO_NEG_LODBIAS_OFF                      = 0x00000000,
    PS_TEXFILTER_NO_NEG_LODBIAS_ON                       = 0x00000001,
    PS_TEXFILTER_NO_NEG_LODBIAS_NUM_VALUES = 2,
    PS_TEXFILTER_NO_NEG_LODBIAS_DEFAULT = PS_TEXFILTER_NO_NEG_LODBIAS_OFF
};

enum EValues_QUALITY_ENHANCEMENTS {
    QUALITY_ENHANCEMENTS_HIGHQUALITY                     = 0xfffffff6,
    QUALITY_ENHANCEMENTS_QUALITY                         = 0x00000000,
    QUALITY_ENHANCEMENTS_PERFORMANCE                     = 0x0000000a,
    QUALITY_ENHANCEMENTS_HIGHPERFORMANCE                 = 0x00000014,
    QUALITY_ENHANCEMENTS_NUM_VALUES = 4,
    QUALITY_ENHANCEMENTS_DEFAULT = QUALITY_ENHANCEMENTS_QUALITY
};

enum EValues_QUALITY_ENHANCEMENT_SUBSTITUTION {
    QUALITY_ENHANCEMENT_SUBSTITUTION_NO_SUBSTITUTION     = 0x00000000,
    QUALITY_ENHANCEMENT_SUBSTITUTION_HIGHQUALITY_BECOMES_QUALITY = 0x00000001,
    QUALITY_ENHANCEMENT_SUBSTITUTION_NUM_VALUES = 2,
    QUALITY_ENHANCEMENT_SUBSTITUTION_DEFAULT = QUALITY_ENHANCEMENT_SUBSTITUTION_NO_SUBSTITUTION
};

enum EValues_REFRESH_RATE_OVERRIDE {
    REFRESH_RATE_OVERRIDE_APPLICATION_CONTROLLED         = 0x00000000,
    REFRESH_RATE_OVERRIDE_HIGHEST_AVAILABLE              = 0x00000001,
    REFRESH_RATE_OVERRIDE_LOW_LATENCY_RR_MASK            = 0x00000FF0,
    REFRESH_RATE_OVERRIDE_NUM_VALUES = 3,
    REFRESH_RATE_OVERRIDE_DEFAULT = REFRESH_RATE_OVERRIDE_APPLICATION_CONTROLLED
};

enum EValues_SET_POWER_THROTTLE_FOR_PCIe_COMPLIANCE {
    SET_POWER_THROTTLE_FOR_PCIe_COMPLIANCE_OFF           = 0x00000000,
    SET_POWER_THROTTLE_FOR_PCIe_COMPLIANCE_ON            = 0x00000001,
    SET_POWER_THROTTLE_FOR_PCIe_COMPLIANCE_NUM_VALUES = 2,
    SET_POWER_THROTTLE_FOR_PCIe_COMPLIANCE_DEFAULT = SET_POWER_THROTTLE_FOR_PCIe_COMPLIANCE_OFF
};

enum EValues_SET_VAB_DATA {
    SET_VAB_DATA_ZERO                                    = 0x00000000,
    SET_VAB_DATA_UINT_ONE                                = 0x00000001,
    SET_VAB_DATA_FLOAT_ONE                               = 0x3f800000,
    SET_VAB_DATA_FLOAT_POS_INF                           = 0x7f800000,
    SET_VAB_DATA_FLOAT_NAN                               = 0x7fc00000,
    SET_VAB_DATA_USE_API_DEFAULTS                        = 0xffffffff,
    SET_VAB_DATA_NUM_VALUES = 6,
    SET_VAB_DATA_DEFAULT = SET_VAB_DATA_USE_API_DEFAULTS
};

enum EValues_VSYNCMODE {
    VSYNCMODE_PASSIVE                                    = 0x60925292,
    VSYNCMODE_FORCEOFF                                   = 0x08416747,
    VSYNCMODE_FORCEON                                    = 0x47814940,
    VSYNCMODE_FLIPINTERVAL2                              = 0x32610244,
    VSYNCMODE_FLIPINTERVAL3                              = 0x71271021,
    VSYNCMODE_FLIPINTERVAL4                              = 0x13245256,
    VSYNCMODE_VIRTUAL                                    = 0x18888888,
    VSYNCMODE_NUM_VALUES = 7,
    VSYNCMODE_DEFAULT = VSYNCMODE_PASSIVE
};

enum EValues_VSYNCTEARCONTROL {
    VSYNCTEARCONTROL_DISABLE                             = 0x96861077,
    VSYNCTEARCONTROL_ENABLE                              = 0x99941284,
    VSYNCTEARCONTROL_NUM_VALUES = 2,
    VSYNCTEARCONTROL_DEFAULT = VSYNCTEARCONTROL_DISABLE
};



typedef struct _SettingDWORDNameString {
    NvU32 settingId;
    const wchar_t * settingNameString;
    NvU32 numSettingValues;
    NvU32 *settingValues;
    NvU32 defaultValue;
} SettingDWORDNameString;

typedef struct _SettingWSTRINGNameString {
    NvU32 settingId;
    const wchar_t * settingNameString;
    NvU32 numSettingValues;
    const wchar_t **settingValues;
    const wchar_t * defaultValue;
} SettingWSTRINGNameString;


#endif // _NVAPI_DRIVER_SETTINGS_H_

