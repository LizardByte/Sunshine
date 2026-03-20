#version 300 es

#ifdef GL_ES
precision mediump float;
#endif

uniform float width_i;
uniform int rotation;

out vec3 uuv;

vec2 rotate_uv(vec2 uv, int rot) {
	if (rot == 90) {
		return vec2(uv.y, 1.0 - uv.x);
	} else if (rot == 180) {
		return vec2(1.0 - uv.x, 1.0 - uv.y);
	} else if (rot == 270) {
		return vec2(1.0 - uv.y, uv.x);
	}
	return uv;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
void main()
{
	float idHigh = float(gl_VertexID >> 1);
	float idLow = float(gl_VertexID & int(1));

	float x = idHigh * 4.0 - 1.0;
	float y = idLow * 4.0 - 1.0;

	float u_base = idHigh * 2.0;
	float v_base = idLow * 2.0;

	// Apply rotation to texture coordinates
	vec2 uv_right = rotate_uv(vec2(u_base, v_base), rotation);
	vec2 uv_left = rotate_uv(vec2(u_base - width_i, v_base), rotation);

	uuv = vec3(uv_left.x, uv_right.x, uv_right.y);
	gl_Position = vec4(x, y, 0.0, 1.0);
}
