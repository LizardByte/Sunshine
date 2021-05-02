Texture2D image : register(t0);

SamplerState def_sampler : register(s0);

struct FragTexWide {
	float3 uuv : TEXCOORD0;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float2 PS(FragTexWide input) : SV_Target
{
	// float4 color_vec_y = { 0.301f, 0.586f, 0.113f, 0.0f };
	// float4 color_vec_u = { -0.168f, -0.328f, 0.496f, 128.0f / 256.0f };
	// float4 color_vec_v = { 0.496f, 0.414f, 0.082f, 128.0f / 256.0f };
	float4 color_vec_y = { 0.299, 0.587, 0.114, 0.0625 };
	float4 color_vec_u = { -0.168736, -0.331264, 0.5, 0.5 };
	float4 color_vec_v = { 0.5, -0.418688, -0.081312, 0.5 };

	// float4 color_vec_y = { 0.2578f, 0.5039f, 0.0977, 0.0625 };
	// float4 color_vec_u = { -0.1484, 0.2891, 0.4375, 128.0f / 256.0f };
	// float4 color_vec_v = { 0.4375, -0.3672, -0.0703, 128.0f / 256.0f };

	float3 rgb_left = image.Sample(def_sampler, input.uuv.xz).rgb;
	float3 rgb_right = image.Sample(def_sampler, input.uuv.yz).rgb;
	float3 rgb = (rgb_left + rgb_right) * 0.5;
	
	float u = dot(color_vec_u.xyz, rgb) + color_vec_u.w;
	float v = dot(color_vec_v.xyz, rgb) + color_vec_v.w;
	return float2(u, v);
}