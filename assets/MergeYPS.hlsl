//--------------------------------------------------------------------------------------
// YCbCrPS2.hlsl
//--------------------------------------------------------------------------------------
Texture2D image : register(t0);

SamplerState def_sampler : register(s0);

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float PS(PS_INPUT frag_in) : SV_Target
{
	float4 color_vec_y = { 0.299, 0.587, 0.114, 0.0625 };
	float4 color_vec_u = { -0.168736, -0.331264, 0.5, 0.5 };
	float4 color_vec_v = { 0.5, -0.418688, -0.081312, 0.5 };

	float3 rgb = image.Load(int3(frag_in.pos.xy, 0)).rgb;
	float y = dot(color_vec_y.xyz, rgb) + color_vec_y.w;
	return y;
}