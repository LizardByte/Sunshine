cbuffer rotate_texture_steps_cbuffer : register(b1) {
    int rotate_texture_steps;
};

#ifdef PLANAR_VIEWPORTS
cbuffer color_matrix_cbuffer : register(b3) {
    float4 color_vec_y;
    float4 color_vec_u;
    float4 color_vec_v;
    float2 range_y;
    float2 range_uv;
};
#endif

struct input_t
{
    float2 viewport_pos : SV_Position;
    float2 tex_coord : TEXCOORD;
#ifdef PLANAR_VIEWPORTS
    uint viewport : SV_ViewportArrayIndex;
#endif
}

#include "include/base_vs_types.hlsl"

vertex_t main_vs(input_t input)
{
    vertex_t output;
    float2 tex_coord;

    tex_coord = input.tex_coord;
    if (rotate_texture_steps != 0) {
        float rotation_radians = radians(90 * rotate_texture_steps);
        float2x2 rotation_matrix = { cos(rotation_radians), -sin(rotation_radians),
                                     sin(rotation_radians), cos(rotation_radians) };
        float2 rotation_center = { 0.5, 0.5 };
        tex_coord = round(rotation_center + mul(rotation_matrix, tex_coord - rotation_center));
    }

    output.viewpoint_pos = float4(input.viewport_pos.x, input.viewport_pos.y, 0, 1);
    output.tex_coord = tex_coord;

#ifdef PLANAR_VIEWPORTS
    output.viewport = input.viewport;
    if (output.viewport == 0) {
        output.color_vec = color_vec_y;
    }
    else if (output.viewport == 1) {
        output.color_vec = color_vec_u;
    }
    else {
        output.color_vec = color_vec_v;
    }
#endif

    return output;
}
