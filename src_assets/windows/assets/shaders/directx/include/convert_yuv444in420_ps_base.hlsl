Texture2D image : register(t0);
SamplerState def_sampler : register(s0);

#ifndef PLANAR_VIEWPORTS
cbuffer color_matrix_cbuffer : register(b0) {
    float4 color_vec_y;
    float4 color_vec_u;
    float4 color_vec_v;
    float2 range_y;
    float2 range_uv;
};
#endif

#include "include/base_vs_types.hlsl"

#ifdef PLANAR_VIEWPORTS
uint main_ps(vertex_t input) : SV_Target
#else
uint2 main_ps(vertex_t input) : SV_Target
#endif
{
    //       Y         U     V
    //       +-------+ +---+ +---+
    //       |       | |   | |   |
    // VP0-> |   Y   | |UR | |VR |
    //       |       | |   | |   |
    //       +---+---+ +---+ +---+
    //       |   |   |
    // VP1-> |UL |VL | <-VP2
    //       |   |   |
    //       +---+---+

    float3 rgb = CONVERT_FUNCTION(image.Sample(def_sampler, input.tex_coord, 0).rgb);

#ifdef PLANAR_VIEWPORTS
    float y = dot(input.color_vec.xyz, rgb) + input.color_vec.w;
    return uint(y);
#else
    float u = dot(color_vec_u.xyz, rgb) + color_vec_u.w;
    float v = dot(color_vec_v.xyz, rgb) + color_vec_v.w;
    return uint2(u, v);
#endif
}
