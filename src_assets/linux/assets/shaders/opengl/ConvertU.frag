#version 300 es

#ifdef GL_ES
precision lowp float;
#endif

uniform sampler2D image;

layout(shared) uniform ColorMatrix {
  vec4 color_vec_y;
  vec4 color_vec_u;
  vec4 color_vec_v;
  vec2 range_y;
  vec2 range_uv;
};

in vec2 tex;
layout(location = 0) out float color;

void main()
{
	vec3 rgb = texture(image, tex).rgb;
	float u = dot(color_vec_u.xyz, rgb) + color_vec_u.w;

	color = u * range_uv.x + range_uv.y;
}