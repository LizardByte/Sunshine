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

#include "include/base_vs_types.hlsl"

vertex_t main_vs(uint vertex_id : SV_VertexID)
{
    vertex_t output;
    float2 tex_coord;

    // id=1 -> +
    //         |\
    //         | \
    //         |  \
    //         +---+
    //         |   |\
    // id=0 -> +---+-+ <- id=3

    //       Y         U     V
    //       +-------+ +---+ +---+
    //       |       | |   | |   |
    // VP0-> |   Y   | |UR | |VR |
    //       |       | |   | |   |
    //       +---+---+ +---+ +---+
    //       |   |   |
    // VP1-> |UL |VL | <-VP2
    //       |   |   |
    //       +---+---+

#ifdef PLANAR_VIEWPORTS
    switch (vertex_id) {
        case 0:
            output.viewpoint_pos = float4(-1, -1, 0, 1);
            tex_coord = float2(0, 1);
            output.viewport = 0;
            output.color_vec = color_vec_y;
            break;
        case 1:
            output.viewpoint_pos = float4(-1, 3, 0, 1);
            tex_coord = float2(0, -1);
            output.viewport = 0;
            output.color_vec = color_vec_y;
            break;
        case 2:
            output.viewpoint_pos = float4(3, -1, 0, 1);
            tex_coord = float2(2, 1);
            output.viewport = 0;
            output.color_vec = color_vec_y;
            break;

        case 3:
            output.viewpoint_pos = float4(-1, -1, 0, 1);
            tex_coord = float2(0, 1);
            output.viewport = 1;
            output.color_vec = color_vec_u;
            break;
        case 4:
            output.viewpoint_pos = float4(-1, 3, 0, 1);
            tex_coord = float2(0, -1);
            output.viewport = 1;
            output.color_vec = color_vec_u;
            break;
        case 5:
            output.viewpoint_pos = float4(7, -1, 0, 1);
            tex_coord = float2(2, 1);
            output.viewport = 1;
            output.color_vec = color_vec_u;
            break;

        case 6:
            output.viewpoint_pos = float4(-1, -1, 0, 1);
            tex_coord = float2(0, 1);
            output.viewport = 2;
            output.color_vec = color_vec_v;
            break;
        case 7:
            output.viewpoint_pos = float4(-1, 3, 0, 1);
            tex_coord = float2(0, -1);
            output.viewport = 2;
            output.color_vec = color_vec_v;
            break;
        case 8:
            output.viewpoint_pos = float4(7, -1, 0, 1);
            tex_coord = float2(2, 1);
            output.viewport = 2;
            output.color_vec = color_vec_v;
            break;
    }
#else
    if (vertex_id == 0) {
        output.viewpoint_pos = float4(-3, -1, 0, 1);
        tex_coord = float2(0, 1);
    }
    else if (vertex_id == 1) {
        output.viewpoint_pos = float4(-3, 5, 0, 1);
        tex_coord = float2(0, -1);
    }
    else {
        output.viewpoint_pos = float4(3, -1, 0, 1);
        tex_coord = float2(2, 1);
    }
#endif

    if (rotate_texture_steps != 0) {
        float rotation_radians = radians(90 * rotate_texture_steps);
        float2x2 rotation_matrix = { cos(rotation_radians), -sin(rotation_radians),
                                     sin(rotation_radians), cos(rotation_radians) };
        float2 rotation_center = { 0.5, 0.5 };
        tex_coord = round(rotation_center + mul(rotation_matrix, tex_coord - rotation_center));
    }
    output.tex_coord = tex_coord;

    return output;
}
