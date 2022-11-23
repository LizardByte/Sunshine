FROM ubuntu:22.04 AS sunshine-base

ARG DEBIAN_FRONTEND=noninteractive

FROM sunshine-base as sunshine-build

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN apt-get update -y \
     && apt-get install -y --no-install-recommends \
        build-essential=12.9* \
        cmake=3.22.1* \
        libavdevice-dev=7:4.4.* \
        libboost-filesystem-dev=1.74.0* \
        libboost-log-dev=1.74.0* \
        libboost-thread-dev=1.74.0* \
        libcap-dev=1:2.44* \
        libcurl4-openssl-dev=7.81.0* \
        libdrm-dev=2.4.110* \
        libevdev-dev=1.12.1* \
        libpulse-dev=1:15.99.1* \
        libopus-dev=1.3.1* \
        libssl-dev=3.0.2* \
        libva-dev=2.14.0* \
        libvdpau-dev=1.1.1* \
        libwayland-dev=1.20.0* \
        libx11-dev=2:1.7.5* \
        libxcb-shm0-dev=1.14* \
        libxcb-xfixes0-dev=1.14* \
        libxcb1-dev=1.14* \
        libxfixes-dev=1:6.0.0* \
        libxrandr-dev=2:1.5.2* \
        libxtst-dev=2:1.2.3* \
        nodejs=12.22.9* \
        npm=8.5.1* \
        nvidia-cuda-dev=11.5.1* \
        nvidia-cuda-toolkit=11.5.1* \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# copy repository
WORKDIR /root/sunshine-build/
COPY . .

# setup npm and dependencies
WORKDIR /root/sunshine-build/src_assets/common/assets/web
RUN npm install

# setup build directory
WORKDIR /root/sunshine-build/build

# cmake and cpack
RUN cmake -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DSUNSHINE_ASSETS_DIR=share/sunshine \
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
    && apt-get install -y --no-install-recommends /sunshine.deb \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# network setup
EXPOSE 47984-47990/tcp
EXPOSE 48010
EXPOSE 47998-48000/udp

# setup user
ARG PGID=1000
ENV PGID=${PGID}
ARG PUID=1000
ENV PUID=${PUID}
ENV TZ="UTC"
ARG UNAME=lizard
ENV UNAME=${UNAME}

ENV HOME=/home/$UNAME

RUN groupadd -f -g "${PGID}" "${UNAME}" && \
    useradd -lm -d ${HOME} -s /bin/bash -g "${PGID}" -G input -u "${PUID}" "${UNAME}" && \
    mkdir -p ${HOME}/.config/sunshine && \
    ln -s ${HOME}/.config/sunshine /config && \
    chown -R ${UNAME} ${HOME}

USER ${UNAME}
WORKDIR ${HOME}

# entrypoint
ENTRYPOINT ["/usr/bin/sunshine"]
