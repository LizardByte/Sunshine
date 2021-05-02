struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD;
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS(uint vI : SV_VERTEXID)
{
	float idHigh = float(vI >> 1);
	float idLow = float(vI & uint(1));

	float x = idHigh * 4.0 - 1.0;
	float y = idLow * 4.0 - 1.0;

	PS_INPUT vert_out;
	vert_out.pos = float4(x, y, 0.0, 1.0);
	vert_out.tex = float2(idHigh, idLow);
	return vert_out;
}