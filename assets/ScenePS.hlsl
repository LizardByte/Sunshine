Texture2D image : register(t0);

SamplerState def_sampler : register(s0);

cbuffer ColorMatrix : register(b0) {
	float4 color_vec_y;
	float4 color_vec_u;
	float4 color_vec_v;
};

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 main_ps(PS_INPUT frag_in) : SV_Target
{
	float4 color = image.Sample(def_sampler, frag_in.tex, 0);

  clip(color.a < 0.1f ? -1 : 1);
	return color;
}