struct ShaderInput
{
    float2 pos : POSITION;
    float2 tex : TEXCOORD0;
};

struct ShaderOutput
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

ShaderOutput main(ShaderInput input) 
{
    ShaderOutput output;
    output.pos = float4(input.pos, 0.0, 1.0);
    output.tex = input.tex;
    return output;
}