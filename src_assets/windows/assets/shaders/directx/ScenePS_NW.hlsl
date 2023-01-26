Texture2D image : register(t0);

SamplerState def_sampler : register(s0);

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};

cbuffer SdrScaling : register(b0) {
    float scale_factor;
};

float4 main_ps(PS_INPUT frag_in) : SV_Target
{
    float4 rgba = image.Sample(def_sampler, frag_in.tex, 0);

    rgba.rgb = rgba.rgb * scale_factor;

    return rgba;
}
