#include "d3d11_yuv444_pixel_start.hlsli"

min16float3 swizzle(min16float3 input)
{
    // Y410 SRVs are in UYVA order
    return input.grb;
}

#include "d3d11_yuv444_pixel_end.hlsli"