cbuffer rotate_texture_steps_cbuffer : register(b1) {
    int rotate_texture_steps;
};

cbuffer source_crop_cbuffer : register(b2) {
    float2 source_crop_scale;
};

#include "include/base_vs.hlsl"

vertex_t main_vs(uint vertex_id : SV_VertexID)
{
    return generate_fullscreen_triangle_vertex(vertex_id, float2(0, 0), rotate_texture_steps, source_crop_scale);
}
