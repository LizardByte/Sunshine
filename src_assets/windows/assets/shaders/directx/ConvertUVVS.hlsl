struct VertTexPosWide {
    float3 uuv : TEXCOORD;
    float4 pos : SV_POSITION;
};

cbuffer info : register(b0) {
    float width_i;
};

cbuffer rotation_info : register(b1) {
    int rotation;
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
VertTexPosWide main_vs(uint vI : SV_VERTEXID)
{
    VertTexPosWide output;
    float2 tex;

    if (vI == 0) {
        output.pos = float4(-1, -1, 0, 1);
        tex = float2(0, 1);
    }
    else if (vI == 1) {
        output.pos = float4(-1, 3, 0, 1);
        tex = float2(0, -1);
    }
    else if (vI == 2) {
        output.pos = float4(3, -1, 0, 1);
        tex = float2(2, 1);
    }

    if (rotation != 0) {
        float rotation_radians = radians(90 * rotation);
        float2x2 rotation_matrix = { cos(rotation_radians), -sin(rotation_radians),
                                     sin(rotation_radians), cos(rotation_radians) };
        float2 rotation_center = { 0.5, 0.5 };
        tex = round(rotation_center + mul(rotation_matrix, tex - rotation_center));
    }

    output.uuv = float3(tex.x, tex.x - width_i, tex.y);

    return output;
}
