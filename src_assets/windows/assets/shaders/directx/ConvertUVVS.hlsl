struct VertTexPosWide {
	float3 uuv : TEXCOORD;
	float4 pos : SV_POSITION;
};

cbuffer info : register(b0) {
	float width_i;
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
VertTexPosWide main_vs(uint vI : SV_VERTEXID)
{
	float idHigh = float(vI >> 1);
	float idLow = float(vI & uint(1));

	float x = idHigh * 4.0 - 1.0;
	float y = idLow * 4.0 - 1.0;

	float u_right = idHigh * 2.0;
	float u_left = u_right - width_i;
	float v = 1.0 - idLow * 2.0;

	VertTexPosWide vert_out;
	vert_out.uuv = float3(u_left, u_right, v);
	vert_out.pos = float4(x, y, 0.0, 1.0);
	return vert_out;
}