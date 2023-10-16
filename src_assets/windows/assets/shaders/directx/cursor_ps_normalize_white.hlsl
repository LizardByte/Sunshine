Texture2D cursor : register(t0);
SamplerState def_sampler : register(s0);

cbuffer normalize_white_cbuffer : register(b1) {
    float white_multiplier;
};

#include "include/base_vs_types.hlsl"

float4 main_ps(vertex_t input) : SV_Target
{
    float4 output = cursor.Sample(def_sampler, input.tex_coord, 0);

    output.rgb = output.rgb * white_multiplier;

    return output;
}
