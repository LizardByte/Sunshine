struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};

cbuffer rotation_info : register(b2) {
    int rotation;
};

PS_INPUT main_vs(uint vI : SV_VERTEXID)
{
    PS_INPUT output;

    if (vI == 0) {
        output.pos = float4(-1, -1, 0, 1);
        output.tex = float2(0, 1);
    }
    else if (vI == 1) {
        output.pos = float4(-1, 3, 0, 1);
        output.tex = float2(0, -1);
    }
    else if (vI == 2) {
        output.pos = float4(3, -1, 0, 1);
        output.tex = float2(2, 1);
    }

    if (rotation != 0) {
        float rotation_radians = radians(90 * rotation);
        float2x2 rotation_matrix = { cos(rotation_radians), -sin(rotation_radians),
                                     sin(rotation_radians), cos(rotation_radians) };
        float2 rotation_center = { 0.5, 0.5 };
        output.tex = round(rotation_center + mul(rotation_matrix, output.tex - rotation_center));
    }

    return output;
}