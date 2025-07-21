#include "include/base_vs_types.hlsl"

vertex_t generate_fullscreen_triangle_vertex(uint vertex_id, float2 subsample_offset, int rotate_texture_steps)
{
    vertex_t output;
    float2 tex_coord;

    if (vertex_id == 0) {
        output.viewpoint_pos = float4(-1, -1, 0, 1);
        tex_coord = float2(0, 1);
    }
    else if (vertex_id == 1) {
        output.viewpoint_pos = float4(-1, 3, 0, 1);
        tex_coord = float2(0, -1);
    }
    else {
        output.viewpoint_pos = float4(3, -1, 0, 1);
        tex_coord = float2(2, 1);
    }

    if (rotate_texture_steps != 0) {
        float rotation_radians = radians(90 * rotate_texture_steps);
        float2x2 rotation_matrix = { cos(rotation_radians), -sin(rotation_radians),
                                     sin(rotation_radians), cos(rotation_radians) };
        float2 rotation_center = { 0.5, 0.5 };
        tex_coord = round(rotation_center + mul(rotation_matrix, tex_coord - rotation_center));

        // Swap the xy offset coordinates if the texture is rotated an odd number of times.
        if (rotate_texture_steps & 1) {
            subsample_offset.xy = subsample_offset.yx;
        }
    }

#if defined(LEFT_SUBSAMPLING)
    output.tex_right_left_center = float3(tex_coord.x, tex_coord.x - subsample_offset.x, tex_coord.y);
#elif defined(LEFT_SUBSAMPLING_SCALE)
    float2 halfsample_offset = subsample_offset / 2;
    float3 right_center_left = float3(tex_coord.x + halfsample_offset.x,
                                      tex_coord.x - halfsample_offset.x,
                                      tex_coord.x - 3 * halfsample_offset.x);
    float2 top_bottom = float2(tex_coord.y - halfsample_offset.y,
                               tex_coord.y + halfsample_offset.y);
    output.tex_right_center_left_top = float4(right_center_left, top_bottom.x);
    output.tex_right_center_left_bottom = float4(right_center_left, top_bottom.y);
#elif defined(TOPLEFT_SUBSAMPLING)
    output.tex_right_left_top = float3(tex_coord.x, tex_coord.x - subsample_offset.x, tex_coord.y - subsample_offset.y);
    output.tex_right_left_bottom = float3(tex_coord.x, tex_coord.x - subsample_offset.x, tex_coord.y);
#else
    output.tex_coord = tex_coord;
#endif

    return output;
}
