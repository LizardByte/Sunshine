struct vertex_t
{
    float4 viewpoint_pos : SV_Position;
#if defined(LEFT_SUBSAMPLING) || defined(TOPLEFT_SUBSAMPLING)
    float3 tex_right_left_top : TEXCOORD0;
    float3 tex_right_left_bottom : TEXCOORD1;
#else
    float2 tex_coord : TEXCOORD;
#endif
};
