Texture2D image : register(t0);
SamplerState def_sampler : register(s0);

#define PLANAR_VIEWPORTS
#include "include/base_vs_types.hlsl"

#ifdef RECOMBINED444_V_SAMPLING
uint2 main_ps(vertex_t input) : SV_Target
#else
uint main_ps(vertex_t input) : SV_Target
#endif
{
    //   Vertical stacking    |           Horizontal stacking
    //                        |
    //     Y       U     V    |          Y             U         V
    // +-------+ +---+ +---+  |  +-------+-------+ +---+---+ +---+---+
    // |       | |V0 | |V1 |  |  |       |       | |V0 |V2 | |V1 |V3 |
    // |   Y   | +---+ +---+  |  |   Y   |   U   | +---+---+ +---+---+
    // |       | |V2 | |V3 |  |  |       |       |
    // +-------+ +---+ +---+  |  +-------+-------+
    // |       |              |
    // |   U   |              |
    // |       |              |
    // +-------+              |

#ifdef RECOMBINED444_V_SAMPLING
    float3 rgb_0_or_2 = CONVERT_FUNCTION(image.Sample(def_sampler, input.tex_right_left_center.yz).rgb);
    float3 rgb_1_or_3 = CONVERT_FUNCTION(image.Sample(def_sampler, input.tex_right_left_center.xz).rgb);
    uint2 vv = uint2(dot(input.color_vec.xyz, rgb_0_or_2) + input.color_vec.w,
                     dot(input.color_vec.xyz, rgb_1_or_3) + input.color_vec.w);
#ifdef P010
    return vv << 6;
#else
    return vv;
#endif
#else
    float3 rgb = CONVERT_FUNCTION(image.Sample(def_sampler, input.tex_coord, 0).rgb);
    uint y_or_u = dot(input.color_vec.xyz, rgb) + input.color_vec.w;
#ifdef P010
    return y_or_u << 6;
#else
    return y_or_u;
#endif
#endif
}
