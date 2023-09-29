Texture2D image : register(t0);
SamplerState def_sampler : register(s0);

cbuffer color_matrix_cbuffer : register(b0) {
    float4 color_vec_y;
    float4 color_vec_u;
    float4 color_vec_v;
    float2 range_y;
    float2 range_uv;
};

#include "include/base_vs_types.hlsl"

#ifdef MULTIVIEW
float main_ps(vertex_t input) : SV_Target
#else
float3 main_ps(vertex_t input) : SV_Target
#endif
{
    float3 rgb = CONVERT_FUNCTION(image.Sample(def_sampler, input.tex_coord, 0).rgb);

#ifdef MULTIVIEW
    float result;
    if (input.viewport == 0) {
        result = dot(color_vec_y.xyz, rgb) + color_vec_y.w;
        result = result * range_y.x + range_y.y;
    }
    else if (input.viewport == 1) {
        result = dot(color_vec_u.xyz, rgb) + color_vec_u.w;
        result = result * range_uv.x + range_uv.y;
    }
    else {
        result = dot(color_vec_v.xyz, rgb) + color_vec_v.w;
        result = result * range_uv.x + range_uv.y;
    }
    return result;
#else
    float y = dot(color_vec_y.xyz, rgb) + color_vec_y.w;
    float u = dot(color_vec_u.xyz, rgb) + color_vec_u.w;
    float v = dot(color_vec_v.xyz, rgb) + color_vec_v.w;

    return float3(y * range_y.x + range_y.y, u * range_uv.x + range_uv.y, v * range_uv.x + range_uv.y);
#endif
}
