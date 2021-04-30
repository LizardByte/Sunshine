//--------------------------------------------------------------------------------------
// CombinedUVMipsPS.hlsl
//--------------------------------------------------------------------------------------
Texture2D txInputU : register(t0);
Texture2D txInputV : register(t1);
Texture1D txInputShift : register(t2);

SamplerState GenericSampler : register(s0);

struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float PS(PS_INPUT input) : SV_Target
{
	float fShift = (float)txInputShift.Sample(GenericSampler, input.Tex.x);

	if(fShift == 0.0f)
		return (float)txInputU.SampleLevel(GenericSampler, input.Tex, 1.0f);
	else
		return (float)txInputV.SampleLevel(GenericSampler, input.Tex, 1.0f);
}