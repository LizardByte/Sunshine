#version 300 es
#extension GL_OES_EGL_image_external : require
precision mediump float;
out vec4 FragColor;

in vec2 vTextCoord;

uniform samplerExternalOES uTexture;

void main() {
        FragColor = texture2D(uTexture, vTextCoord);
}
