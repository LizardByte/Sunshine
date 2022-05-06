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

in vec3 uuv;
layout(location = 0) out vec2 color;

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
void main() {
  vec3 rgb_left  = texture(image, uuv.xz).rgb;
  vec3 rgb_right = texture(image, uuv.yz).rgb;
  vec3 rgb       = (rgb_left + rgb_right) * 0.5;

  float u = dot(color_vec_u.xyz, rgb) + color_vec_u.w;
  float v = dot(color_vec_v.xyz, rgb) + color_vec_v.w;

  u = u * range_uv.x + range_uv.y;
  v = v * range_uv.x + range_uv.y;

  color = vec2(u, v * 224.0f / 256.0f + 0.0625);
}