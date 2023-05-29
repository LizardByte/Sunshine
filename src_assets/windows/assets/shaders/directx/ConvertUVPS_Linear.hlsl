Texture2D image : register(t0);

SamplerState def_sampler : register(s0);

struct FragTexWide {
	float3 uuv : TEXCOORD0;
};

cbuffer ColorMatrix : register(b0) {
	float4 color_vec_y;
	float4 color_vec_u;
	float4 color_vec_v;
	float2 range_y;
	float2 range_uv;
};

// This is a fast sRGB approximation from Microsoft's ColorSpaceUtility.hlsli
float3 ApplySRGBCurve(float3 x)
{
	return x < 0.0031308 ? 12.92 * x : 1.13005 * sqrt(x - 0.00228) - 0.13448 * x + 0.005719;
}

float2 main_ps(FragTexWide input) : SV_Target
{
	float3 rgb_left = ApplySRGBCurve(saturate(image.Sample(def_sampler, input.uuv.xz)).rgb);
	float3 rgb_right = ApplySRGBCurve(saturate(image.Sample(def_sampler, input.uuv.yz)).rgb);
	float3 rgb = (rgb_left + rgb_right) * 0.5;
	
	float u = dot(color_vec_u.xyz, rgb) + color_vec_u.w;
	float v = dot(color_vec_v.xyz, rgb) + color_vec_v.w;

	u = u * range_uv.x + range_uv.y;
	v = v * range_uv.x + range_uv.y;

	return float2(u, v);
}