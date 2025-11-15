SOURCES = main.cpp

CONFIG += link_pkgconfig

# This adds /opt/vc/include to the include path which
# pulls in the BRCM GLES and EGL libraries. If we don't
# add this, we'll get the system's headers which expose
# functionality the runtime GL implementation won't have.
packagesExist(mmal) {
    PKGCONFIG += mmal
}

PKGCONFIG += sdl2 egl libavcodec libavutil