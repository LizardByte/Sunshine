Texture2D image : register(t0);

SamplerState def_sampler : register(s0);

struct FragTexWide {
	float3 uuv : TEXCOORD0;
};

cbuffer ColorMatrix : register(b0) {
	float4 color_vec_y;
	float4 color_vec_u;
	float4 color_vec_v;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float2 PS(FragTexWide input) : SV_Target
{
	float3 rgb_left = image.Sample(def_sampler, input.uuv.xz).rgb;
	float3 rgb_right = image.Sample(def_sampler, input.uuv.yz).rgb;
	float3 rgb = (rgb_left + rgb_right) * 0.5;
	
	float u = dot(color_vec_u.xyz, rgb) + color_vec_u.w;
	float v = dot(color_vec_v.xyz, rgb) + color_vec_v.w;

	return float2(u, v);
}