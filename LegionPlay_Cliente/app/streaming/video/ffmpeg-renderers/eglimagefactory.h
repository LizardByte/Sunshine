#pragma once

#include "renderer.h"

#ifdef HAVE_LIBVA
#include <va/va_drmcommon.h>
#endif

class EglImageFactory
{
public:
    EglImageFactory(IFFmpegRenderer* renderer);
    bool initializeEGL(EGLDisplay, const EGLExtensions &ext);

#ifdef HAVE_DRM
    ssize_t exportDRMImages(AVFrame* frame, AVDRMFrameDescriptor* drmFrame, EGLDisplay dpy, EGLImage images[EGL_MAX_PLANES]);
#endif

#ifdef HAVE_LIBVA
    ssize_t exportVAImages(AVFrame* frame, VADRMPRIMESurfaceDescriptor* vaFrame, EGLDisplay dpy, EGLImage images[EGL_MAX_PLANES]);
#endif

    bool supportsImportingFormat(EGLDisplay dpy, EGLint format);
    bool supportsImportingModifier(EGLDisplay dpy, EGLint format, EGLuint64KHR modifier);

    void freeEGLImages(EGLDisplay dpy, EGLImage images[EGL_MAX_PLANES]);

private:
    IFFmpegRenderer* m_Renderer;
    bool m_EGLExtDmaBuf;
    PFNEGLCREATEIMAGEPROC m_eglCreateImage;
    PFNEGLDESTROYIMAGEPROC m_eglDestroyImage;
    PFNEGLCREATEIMAGEKHRPROC m_eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC m_eglDestroyImageKHR;
    PFNEGLQUERYDMABUFFORMATSEXTPROC m_eglQueryDmaBufFormatsEXT;
    PFNEGLQUERYDMABUFMODIFIERSEXTPROC m_eglQueryDmaBufModifiersEXT;
};
