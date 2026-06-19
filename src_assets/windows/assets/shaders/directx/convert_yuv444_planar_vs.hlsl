cbuffer rotate_texture_steps_cbuffer : register(b1) {
    int rotate_texture_steps;
};

cbuffer color_matrix_cbuffer : register(b3) {
    float4 color_vec_y;
    float4 color_vec_u;
    float4 color_vec_v;
    float2 range_y;
    float2 range_uv;
};

#define PLANAR_VIEWPORTS
#include "include/base_vs.hlsl"

vertex_t main_vs(uint vertex_id : SV_VertexID)
{
    vertex_t output = generate_fullscreen_triangle_vertex(vertex_id % 3, float2(0, 0), rotate_texture_steps);

    output.viewport = vertex_id / 3;

    if (output.viewport == 0) {
        output.color_vec = color_vec_y;
    }
    else if (output.viewport == 1) {
        output.color_vec = color_vec_u;
    }
    else {
        output.color_vec = color_vec_v;
    }

    return output;
}
