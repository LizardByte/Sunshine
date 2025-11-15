#pragma once

#include "renderer.h"

#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS 1
#include <SDL_egl.h>
#include <SDL_opengles2.h>

class EGLRenderer : public IFFmpegRenderer {
public:
    EGLRenderer(IFFmpegRenderer *backendRenderer);
    virtual ~EGLRenderer() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary** options) override;
    virtual void cleanupRenderContext() override;
    virtual void waitToRender() override;
    virtual void prepareToRender() override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual bool testRenderFrame(AVFrame* frame) override;
    virtual void notifyOverlayUpdated(Overlay::OverlayType) override;
    virtual bool notifyWindowChanged(PWINDOW_STATE_CHANGE_INFO) override;
    virtual bool isPixelFormatSupported(int videoFormat, enum AVPixelFormat pixelFormat) override;
    virtual AVPixelFormat getPreferredPixelFormat(int videoFormat) override;

private:

    void renderOverlay(Overlay::OverlayType type, int viewportWidth, int viewportHeight);
    unsigned compileShader(const char* vertexShaderSrc, const char* fragmentShaderSrc);
    bool compileShaders();
    bool specialize();
    static int loadAndBuildShader(int shaderType, const char *filename);

    AVPixelFormat m_EGLImagePixelFormat;
    void *m_EGLDisplay;
    unsigned m_Textures[EGL_MAX_PLANES];
    unsigned m_OverlayTextures[Overlay::OverlayMax];
    unsigned m_OverlayVbos[Overlay::OverlayMax];
    SDL_atomic_t m_OverlayHasValidData[Overlay::OverlayMax];
    unsigned m_ShaderProgram;
    unsigned m_OverlayShaderProgram;
    SDL_GLContext m_Context;
    SDL_Window *m_Window;
    IFFmpegRenderer *m_Backend;
    unsigned int m_VAO;
    bool m_BlockingSwapBuffers;
    EGLSync m_LastRenderSync;
    AVFrame* m_LastFrame;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC m_glEGLImageTargetTexture2DOES;
    PFNGLGENVERTEXARRAYSOESPROC m_glGenVertexArraysOES;
    PFNGLBINDVERTEXARRAYOESPROC m_glBindVertexArrayOES;
    PFNGLDELETEVERTEXARRAYSOESPROC m_glDeleteVertexArraysOES;
    PFNEGLCREATESYNCPROC m_eglCreateSync;
    PFNEGLCREATESYNCKHRPROC m_eglCreateSyncKHR;
    PFNEGLDESTROYSYNCPROC m_eglDestroySync;
    PFNEGLCLIENTWAITSYNCPROC m_eglClientWaitSync;
    int m_GlesMajorVersion;
    int m_GlesMinorVersion;
    bool m_HasExtUnpackSubimage;

#define NV12_PARAM_YUVMAT 0
#define NV12_PARAM_OFFSET 1
#define NV12_PARAM_CHROMA_OFFSET 2
#define NV12_PARAM_PLANE1 3
#define NV12_PARAM_PLANE2 4
#define OPAQUE_PARAM_TEXTURE 0
    int m_ShaderProgramParams[5];

#define OVERLAY_PARAM_TEXTURE 0
    int m_OverlayShaderProgramParams[1];

    int m_OldContextProfileMask;
    int m_OldContextMajorVersion;
    int m_OldContextMinorVersion;

    SDL_Renderer *m_DummyRenderer;
};
