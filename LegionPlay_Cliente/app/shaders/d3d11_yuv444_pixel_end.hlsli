min16float4 main(ShaderInput input) : SV_TARGET
{
    // Clamp the texcoords to avoid sampling the row of texels adjacent to the alignment padding
    min16float3 yuv = swizzle(videoTex.Sample(theSampler, min(input.tex, chromaTexMax.rg)));

    // Subtract the YUV offset for limited vs full range
    yuv -= offsets;

    // Multiply by the conversion matrix for this colorspace
    yuv = mul(yuv, cscMatrix);

    return min16float4(yuv, 1.0);
}