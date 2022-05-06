#version 300 es

#ifdef GL_ES
precision lowp float;
#endif

uniform sampler2D image;

in vec2 tex;
layout(location = 0) out vec4 color;
void main()
{
	color = texture(image, tex);
}