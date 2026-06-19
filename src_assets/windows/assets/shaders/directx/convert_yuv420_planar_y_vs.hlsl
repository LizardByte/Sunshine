cbuffer rotate_texture_steps_cbuffer : register(b1) {
    int rotate_texture_steps;
};

#include "include/base_vs.hlsl"

vertex_t main_vs(uint vertex_id : SV_VertexID)
{
    return generate_fullscreen_triangle_vertex(vertex_id, float2(0, 0), rotate_texture_steps);
}
