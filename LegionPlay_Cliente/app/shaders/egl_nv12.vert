#version 300 es

layout (location = 0) in vec2 aPosition; // 2D: X,Y
layout (location = 1) in vec2 aTexCoord;
out vec2 vTextCoord;

void main() {
	vTextCoord = aTexCoord;
	gl_Position = vec4(aPosition, 0, 1);
}
