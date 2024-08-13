struct vertex_t
{
    float4 viewpoint_pos : SV_Position;
#if defined(LEFT_SUBSAMPLING)
    float3 tex_right_left_center : TEXCOORD;
#elif defined(LEFT_SUBSAMPLING_SCALE)
    float4 tex_right_center_left_top : TEXCOORD0;
    float4 tex_right_center_left_bottom : TEXCOORD1;
#elif defined(TOPLEFT_SUBSAMPLING)
    float3 tex_right_left_top : TEXCOORD0;
    float3 tex_right_left_bottom : TEXCOORD1;
#else
    float2 tex_coord : TEXCOORD;
#endif
#ifdef PLANAR_VIEWPORTS
    uint viewport : SV_ViewportArrayIndex;
    nointerpolation float4 color_vec : COLOR0;
#endif
};
