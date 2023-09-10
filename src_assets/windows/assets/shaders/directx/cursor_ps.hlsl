Texture2D cursor : register(t0);
SamplerState def_sampler : register(s0);

#include "include/base_vs_types.hlsl"

float4 main_ps(vertex_t input) : SV_Target
{
    return cursor.Sample(def_sampler, input.tex_coord, 0);
}
