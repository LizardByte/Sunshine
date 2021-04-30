//--------------------------------------------------------------------------------------
// ScreenPS.hlsl
//--------------------------------------------------------------------------------------
Texture2D txInput : register(t0);

SamplerState GenericSampler : register(s0);

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float4 PS(PS_INPUT input) : SV_Target
{
	return txInput.Sample(GenericSampler, input.Tex);
}