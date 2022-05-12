#version 300 es

#ifdef GL_ES
precision mediump float;
#endif

uniform float width_i;

out vec3 uuv;
//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
void main()
{
	float idHigh = float(gl_VertexID >> 1);
	float idLow = float(gl_VertexID & int(1));

	float x = idHigh * 4.0 - 1.0;
	float y = idLow * 4.0 - 1.0;

	float u_right = idHigh * 2.0;
	float u_left = u_right - width_i;
	float v = idLow * 2.0;

	uuv = vec3(u_left, u_right, v);
	gl_Position = vec4(x, y, 0.0, 1.0);
}