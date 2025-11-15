#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS 1

#include <SDL_egl.h>
#include <SDL_opengles2.h>

#ifndef EGL_VERSION_1_4
#error EGLRenderer requires EGL 1.4
#endif

#ifndef GL_ES_VERSION_2_0
#error EGLRenderer requires OpenGL ES 2.0
#endif

int main() {
    return 0;
}