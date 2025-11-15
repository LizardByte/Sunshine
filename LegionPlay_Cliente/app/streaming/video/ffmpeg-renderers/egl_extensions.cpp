#include "renderer.h"

static QStringList egl_get_extensions(EGLDisplay dpy) {
    const auto EGLExtensionsStr = eglQueryString(dpy, EGL_EXTENSIONS);
    if (!EGLExtensionsStr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unable to get EGL extensions: %d", eglGetError());
        return QStringList();
    }
    return QString(EGLExtensionsStr).split(" ");
}

EGLExtensions::EGLExtensions(EGLDisplay dpy) :
    m_Extensions(egl_get_extensions(dpy))
{}

bool EGLExtensions::isSupported(const QString &extension) const {
    return m_Extensions.contains(extension);
}
