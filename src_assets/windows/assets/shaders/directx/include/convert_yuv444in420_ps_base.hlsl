Texture2D image : register(t0);
SamplerState def_sampler : register(s0);

#define PLANAR_VIEWPORTS
#include "include/base_vs_types.hlsl"

#ifdef PROTOTYPE_UV_SAMPLING
uint2 main_ps(vertex_t input) : SV_Target
#else
uint main_ps(vertex_t input) : SV_Target
#endif
{
    //     Y       U     V
    // +-------+ +---+ +---+
    // |       | |V0 | |V1 |
    // |   Y   | +---+ +---+
    // |       | |V2 | |V3 |
    // +-------+ +---+ +---+
    // |       |
    // |   U   |
    // |       |
    // +-------+
#ifdef PROTOTYPE_UV_SAMPLING
    float3 rgb_left = CONVERT_FUNCTION(image.Sample(def_sampler, input.tex_right_left_center.yz).rgb);
    float3 rgb_right = CONVERT_FUNCTION(image.Sample(def_sampler, input.tex_right_left_center.xz).rgb);
    uint2 vv = uint2(dot(input.color_vec.xyz, rgb_left) + input.color_vec.w,
                     dot(input.color_vec.xyz, rgb_right) + input.color_vec.w);
#ifdef P010
    return vv << 6;
#else
    return vv;
#endif
#else
    float3 rgb = CONVERT_FUNCTION(image.Sample(def_sampler, input.tex_coord, 0).rgb);
    uint yu = dot(input.color_vec.xyz, rgb) + input.color_vec.w;
#ifdef P010
    return yu << 6;
#else
    return yu;
#endif
#endif
}
