precision mediump float;
uniform sampler2D uTexture;
varying vec2 vTexCoord;

void main() {
    vec4 abgr = texture2D(uTexture, vTexCoord);

    gl_FragColor = abgr;
    gl_FragColor.r = abgr.b;
    gl_FragColor.b = abgr.r;
}
