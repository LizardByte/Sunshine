FROM ubuntu:22.04 AS sunshine-base

ARG DEBIAN_FRONTEND=noninteractive
ARG TZ="America/New_York"

FROM sunshine-base as sunshine-build

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN apt-get update -y \
     && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        libavdevice-dev \
        libboost-filesystem-dev \
        libboost-log-dev \
        libboost-thread-dev \
        libcap-dev \
        libdrm-dev \
        libevdev-dev \
        libpulse-dev \
        libopus-dev \
        libssl-dev \
        libwayland-dev \
        libx11-dev \
        libxcb-shm0-dev \
        libxcb-xfixes0-dev \
        libxcb1-dev \
        libxfixes-dev \
        libxrandr-dev \
        libxtst-dev \
        nvidia-cuda-dev \
        nvidia-cuda-toolkit \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# copy repository
RUN mkdir /root/sunshine-build
WORKDIR /root/sunshine-build/
COPY . .

# setup build directory
RUN mkdir /root/sunshine-build/build
WORKDIR /root/sunshine-build/build

# cmake and cpack
RUN cmake -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/etc \
        -DSUNSHINE_ASSETS_DIR=sunshine/assets \
        -DSUNSHINE_CONFIG_DIR=sunshine/config \
        -DSUNSHINE_EXECUTABLE_PATH=/usr/bin/sunshine \
        -DSUNSHINE_ENABLE_WAYLAND=ON \
        -DSUNSHINE_ENABLE_X11=ON \
        -DSUNSHINE_ENABLE_DRM=ON \
        -DSUNSHINE_ENABLE_CUDA=ON \
        /root/sunshine-build \
    && make -j "$(nproc)" \
    && cpack -G DEB

FROM sunshine-base as sunshine

# copy deb from builder
COPY --from=sunshine-build /root/sunshine-build/build/cpack_artifacts/Sunshine.deb /sunshine.deb

# install sunshine
RUN apt-get update -y \
     && apt-get install -y --no-install-recommends -f /sunshine.deb \
     && apt-get clean \
     && rm -rf /var/lib/apt/lists/*

# network setup
EXPOSE 47984-47990/tcp
EXPOSE 48010
EXPOSE 47998-48000/udp

# setup config directory
RUN mkdir /config

# entrypoint
ENTRYPOINT ["/usr/bin/sunshine", "/config/sunshine.conf"]
