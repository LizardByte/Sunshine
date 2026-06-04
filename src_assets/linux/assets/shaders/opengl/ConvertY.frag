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

uniform int sdr_to_hdr;

vec3 sdr_to_hdr_fn(vec3 color) {
  // 1. Linearize sRGB
  vec3 linear = vec3(
    (color.r <= 0.04045) ? (color.r / 12.92) : pow((color.r + 0.055) / 1.055, 2.4),
    (color.g <= 0.04045) ? (color.g / 12.92) : pow((color.g + 0.055) / 1.055, 2.4),
    (color.b <= 0.04045) ? (color.b / 12.92) : pow((color.b + 0.055) / 1.055, 2.4)
  );

  // 2. Gamut conversion (BT.709 to BT.2020)
  vec3 bt2020 = vec3(
    0.627402 * linear.r + 0.329292 * linear.g + 0.043306 * linear.b,
    0.069095 * linear.r + 0.919544 * linear.g + 0.011360 * linear.b,
    0.016394 * linear.r + 0.088028 * linear.g + 0.895578 * linear.b
  );

  // 3. Apply ST 2084 PQ curve (scale linear by 0.0203 for 203 nits reference SDR white)
  const float m1 = 2610.0 / 16384.0;
  const float m2 = 2523.0 / 32.0;
  const float c1 = 3424.0 / 4096.0;
  const float c2 = 2413.0 / 128.0;
  const float c3 = 2392.0 / 128.0;

  vec3 Lp = pow(clamp(bt2020 * 0.0203, 0.0, 1.0), vec3(m1));
  return pow((c1 + c2 * Lp) / (1.0 + c3 * Lp), vec3(m2));
}

void main()
{
	vec3 rgb = texture(image, tex).rgb;
	if (sdr_to_hdr != 0) {
		rgb = sdr_to_hdr_fn(rgb);
	}
	float y = dot(color_vec_y.xyz, rgb) + color_vec_y.w;

	color = y * range_y.x + range_y.y;
}