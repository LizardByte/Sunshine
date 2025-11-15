using namespace metal;

struct Vertex
{
    float4 position [[ position ]];
    float2 texCoords;
};

struct CscParams
{
    float3 matrix[3];
    float3 offsets;
    float2 chromaOffset;
    float bitnessScaleFactor;
};

constexpr sampler s(coord::normalized, address::clamp_to_edge, filter::linear);

vertex Vertex vs_draw(constant Vertex *vertices [[ buffer(0) ]], uint id [[ vertex_id ]])
{
    return vertices[id];
}

fragment float4 ps_draw_biplanar(Vertex v [[ stage_in ]],
                                 constant CscParams &cscParams [[ buffer(0) ]],
                                 texture2d<float> luminancePlane [[ texture(0) ]],
                                 texture2d<float> chrominancePlane [[ texture(1) ]])
{
    float2 chromaOffset = float2(cscParams.chromaOffset.x / luminancePlane.get_width(),
                                 cscParams.chromaOffset.y / luminancePlane.get_height());
    float3 yuv = float3(luminancePlane.sample(s, v.texCoords).r,
                        chrominancePlane.sample(s, v.texCoords + chromaOffset).rg);
    yuv *= cscParams.bitnessScaleFactor;
    yuv -= cscParams.offsets;

    float3 rgb;
    rgb.r = dot(yuv, cscParams.matrix[0]);
    rgb.g = dot(yuv, cscParams.matrix[1]);
    rgb.b = dot(yuv, cscParams.matrix[2]);
    return float4(rgb, 1.0f);
}

fragment float4 ps_draw_triplanar(Vertex v [[ stage_in ]],
                                  constant CscParams &cscParams [[ buffer(0) ]],
                                  texture2d<float> luminancePlane [[ texture(0) ]],
                                  texture2d<float> chrominancePlaneU [[ texture(1) ]],
                                  texture2d<float> chrominancePlaneV [[ texture(2) ]])
{
    float2 chromaOffset = float2(cscParams.chromaOffset.x / luminancePlane.get_width(),
                                 cscParams.chromaOffset.y / luminancePlane.get_height());
    float3 yuv = float3(luminancePlane.sample(s, v.texCoords).r,
                        chrominancePlaneU.sample(s, v.texCoords + chromaOffset).r,
                        chrominancePlaneV.sample(s, v.texCoords + chromaOffset).r);
    yuv *= cscParams.bitnessScaleFactor;
    yuv -= cscParams.offsets;

    float3 rgb;
    rgb.r = dot(yuv, cscParams.matrix[0]);
    rgb.g = dot(yuv, cscParams.matrix[1]);
    rgb.b = dot(yuv, cscParams.matrix[2]);
    return float4(rgb, 1.0f);
}

fragment float4 ps_draw_rgb(Vertex v [[ stage_in ]],
                            texture2d<float> rgbTexture [[ texture(0) ]])
{
    return rgbTexture.sample(s, v.texCoords);
}
