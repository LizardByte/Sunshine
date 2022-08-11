FROM debian:bullseye AS sunshine-build

ARG DEBIAN_FRONTEND=noninteractive 
ARG TZ="Europe/London"

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN echo deb http://deb.debian.org/debian/ bullseye main contrib non-free | tee /etc/apt/sources.list.d/non-free.list
RUN apt-get update -y && \
    apt-get install --no-install-recommends -y \
        build-essential \
        cmake \
        rpm \
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
        nvidia-cuda-toolkit

RUN mkdir /root/sunshine
WORKDIR /root/sunshine

COPY ./* .
RUN mkdir /root/sunshine-build

# RUN /root/sunshine/scripts/build-sunshine.sh

WORKDIR /root/sunshine-build

RUN cmake \
      "-DCMAKE_BUILD_TYPE=Release" \
      "-DCMAKE_INSTALL_PREFIX=/etc" \
      "-DSUNSHINE_ASSETS_DIR=sunshine/assets" \
      "-DSUNSHINE_CONFIG_DIR=sunshine/config" \
      "-DSUNSHINE_EXECUTABLE_PATH=/usr/bin/sunshine" \
      "-DSUNSHINE_ENABLE_WAYLAND=ON" \
      "-DSUNSHINE_ENABLE_X11=ON" \
      "-DSUNSHINE_ENABLE_DRM=ON" \
      "-DSUNSHINE_ENABLE_CUDA=ON" \
      "/root/sunshine"

RUN make -j ${nproc}

RUN cpack -G DEB
# RUN cpack -G RPM

FROM debian:bullseye-slim AS sunshine

COPY --from=sunshine-build /root/sunshine-build/Sunshine.deb /Sunshine.deb
# COPY --from=sunshine-build /root/sunshine-build/Sunshine.rpm /Sunshine.rpm

RUN apt-get update -y && \
    apt-get install --no-install-recommends -y -f /Sunshine.deb \
    && rm -rf /var/lib/apt/lists/*

# Port configuration
# https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide#manual-port-forwarding-advanced
EXPOSE 47984-47990/tcp
EXPOSE 48010
EXPOSE 48010/udp
EXPOSE 47998-48000/udp

ENTRYPOINT ["/usr/bin/sunshine"]
