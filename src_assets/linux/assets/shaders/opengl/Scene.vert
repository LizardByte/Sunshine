#version 300 es

#ifdef GL_ES
precision mediump float;
#endif

out vec2 tex;

void main()
{
	float idHigh = float(gl_VertexID >> 1);
	float idLow = float(gl_VertexID & int(1));

	float x = idHigh * 4.0 - 1.0;
	float y = idLow * 4.0 - 1.0;

	float u = idHigh * 2.0;
	float v = idLow * 2.0;

	gl_Position = vec4(x, y, 0.0, 1.0);
	tex = vec2(u, v);
}