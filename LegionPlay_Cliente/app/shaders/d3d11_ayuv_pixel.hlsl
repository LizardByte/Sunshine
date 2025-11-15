#include "d3d11_yuv444_pixel_start.hlsli"

min16float3 swizzle(min16float3 input)
{
    // AYUV SRVs are in VUYA order
    return input.bgr;
}

#include "d3d11_yuv444_pixel_end.hlsli"