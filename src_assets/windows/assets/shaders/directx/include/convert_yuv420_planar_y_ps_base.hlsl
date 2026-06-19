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

float main_ps(vertex_t input) : SV_Target
{
    float3 rgb = CONVERT_FUNCTION(image.Sample(def_sampler, input.tex_coord, 0).rgb);

    float y = dot(color_vec_y.xyz, rgb) + color_vec_y.w;

    return y * range_y.x + range_y.y;
}
