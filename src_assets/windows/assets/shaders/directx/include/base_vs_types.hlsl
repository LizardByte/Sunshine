struct vertex_t
{
    float4 viewpoint_pos : SV_Position;
#if defined(LEFT_SUBSAMPLING)
    float3 tex_right_left_center : TEXCOORD;
#elif defined (TOPLEFT_SUBSAMPLING)
    float3 tex_right_left_top : TEXCOORD;
    float3 tex_right_left_bottom : TEXCOORD;
#else
    float2 tex_coord : TEXCOORD;
#endif
};
