//--------------------------------------------------------------------------------------
// ScreenVS.hlsl
//--------------------------------------------------------------------------------------
struct PS_INPUT
{
	float4 Pos : SV_POSITION;
	float2 Tex : TEXCOORD;
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS(uint vI : SV_VERTEXID)
{
	PS_INPUT output = (PS_INPUT)0;

	float2 texcoord = float2(vI & 1, vI >> 1);

	output.Pos = float4((texcoord.x - 0.5f) * 2.0f, -(texcoord.y - 0.5f) * 2.0f, 0.0f, 1.0f);
	output.Tex = texcoord;

	return output;
}