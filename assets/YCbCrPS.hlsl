//--------------------------------------------------------------------------------------
// YCbCrPS2.hlsl
//--------------------------------------------------------------------------------------
Texture2D txInput : register(t0);

SamplerState GenericSampler : register(s0);

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

struct PS_OUTPUT
{
	float ColorY : SV_Target0;
	float2 ColorU: SV_Target1;
	float2 ColorV: SV_Target2;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
PS_OUTPUT PS(PS_INPUT input) : SV_Target
{
	PS_OUTPUT output;

	float4 InputColor = txInput.Sample(GenericSampler, input.Tex);

	// Range 0-255
	output.ColorY = (0.257f * InputColor.r + 0.504f * InputColor.g + 0.098f * InputColor.b) + (16 / 256.0f);
	output.ColorU = (-0.148f * InputColor.r - 0.291f * InputColor.g + 0.439f * InputColor.b) + (128.0f / 256.0f);
	output.ColorV = (0.439f * InputColor.r - 0.368f * InputColor.g - 0.071f * InputColor.b) + (128.0f / 256.0f);

	output.ColorY = clamp(output.ColorY, 0.0f, 255.0f);
	output.ColorU = clamp(output.ColorU, 0.0f, 255.0f);
	output.ColorV = clamp(output.ColorV, 0.0f, 255.0f);

	return output;
}