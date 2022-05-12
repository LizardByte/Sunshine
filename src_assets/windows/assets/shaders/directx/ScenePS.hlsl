Texture2D image : register(t0);

SamplerState def_sampler : register(s0);

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD;
};

float4 main_ps(PS_INPUT frag_in) : SV_Target
{
	return image.Sample(def_sampler, frag_in.tex, 0);
}