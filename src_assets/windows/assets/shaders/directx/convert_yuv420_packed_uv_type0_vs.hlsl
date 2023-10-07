cbuffer subsample_offset_cbuffer : register(b0) {
    float2 subsample_offset;
};

cbuffer rotate_texture_steps_cbuffer : register(b1) {
    int rotate_texture_steps;
};

#define LEFT_SUBSAMPLING
#include "include/base_vs.hlsl"

vertex_t main_vs(uint vertex_id : SV_VertexID)
{
    return generate_fullscreen_triangle_vertex(vertex_id, subsample_offset.x, rotate_texture_steps);
}
