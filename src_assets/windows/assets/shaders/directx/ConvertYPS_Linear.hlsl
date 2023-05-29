Texture2D image : register(t0);

SamplerState def_sampler : register(s0);

cbuffer ColorMatrix : register(b0) {
	float4 color_vec_y;
	float4 color_vec_u;
	float4 color_vec_v;
	float2 range_y;
	float2 range_uv;
};

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD;
};

// This is a fast sRGB approximation from Microsoft's ColorSpaceUtility.hlsli
float3 ApplySRGBCurve(float3 x)
{
	return x < 0.0031308 ? 12.92 * x : 1.13005 * sqrt(x - 0.00228) - 0.13448 * x + 0.005719;
}

float main_ps(PS_INPUT frag_in) : SV_Target
{
	float3 rgb = ApplySRGBCurve(saturate(image.Sample(def_sampler, frag_in.tex, 0)).rgb);
	float y = dot(color_vec_y.xyz, rgb) + color_vec_y.w;

	return y * range_y.x + range_y.y;
}