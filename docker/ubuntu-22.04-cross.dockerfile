# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64,linux/arm64/v8
# platforms_pr: linux/amd64
# no-cache-filters: sunshine-base,artifacts,sunshine
ARG BASE=ubuntu
ARG TAG=22.04
FROM ${BASE}:${TAG} AS sunshine-base

ENV DEBIAN_FRONTEND=noninteractive

FROM sunshine-base as sunshine-build

ARG TAG
ARG TARGETARCH=arm64
ARG TUPLE=aarch64-linux-gnu

ENV TAG=${TAG}
ENV TARGETARCH=${TARGETARCH}
ENV TUPLE=${TUPLE}

ARG BRANCH
ARG BUILD_VERSION
ARG COMMIT
# note: BUILD_VERSION may be blank

ENV BRANCH=${BRANCH}
ENV BUILD_VERSION=${BUILD_VERSION}
ENV COMMIT=${COMMIT}

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
# install dependencies
RUN --mount=type=cache,target=/var/cache/apt/archives,mode=0755,id=ubuntu-22.04-apt <<_DEPS
#!/bin/bash
set -e

# Keep downloaded archives in the cache.
rm /etc/apt/apt.conf.d/docker-clean

source /etc/lsb-release
dpkg --add-architecture "${TARGETARCH}"
sed -i "s/^deb /deb [arch=$(dpkg --print-architecture)] /" /etc/apt/sources.list

cat > /etc/apt/sources.list.d/ports.list <<_SOURCES
deb [arch=${TARGETARCH}] http://ports.ubuntu.com/ ${DISTRIB_CODENAME} main restricted universe multiverse
deb [arch=${TARGETARCH}] http://ports.ubuntu.com/ ${DISTRIB_CODENAME}-updates main restricted universe multiverse
_SOURCES

apt-get update -y
apt-get install -y --no-install-recommends \
  ca-certificates \
  cmake=3.22.* \
  gcc-"${TUPLE}" \
  g++-"${TUPLE}" \
  git \
  libwayland-bin \
  nvidia-cuda-toolkit \
  pkgconf \
  wget \
  $([[ "${TARGETARCH}" == amd64 ]] && echo libmfx-dev:"${TARGETARCH}") \
  libayatana-appindicator3-dev:"${TARGETARCH}" \
  libavdevice-dev:"${TARGETARCH}" \
  libboost-filesystem-dev:"${TARGETARCH}"=1.74.* \
  libboost-locale-dev:"${TARGETARCH}"=1.74.* \
  libboost-log-dev:"${TARGETARCH}"=1.74.* \
  libboost-program-options-dev:"${TARGETARCH}"=1.74.* \
  libcap-dev:"${TARGETARCH}" \
  libcurl4-openssl-dev:"${TARGETARCH}" \
  libdrm-dev:"${TARGETARCH}" \
  libevdev-dev:"${TARGETARCH}" \
  libminiupnpc-dev:"${TARGETARCH}" \
  libnotify-dev:"${TARGETARCH}" \
  libnuma-dev:"${TARGETARCH}" \
  libopus-dev:"${TARGETARCH}" \
  libpulse-dev:"${TARGETARCH}" \
  libssl-dev:"${TARGETARCH}" \
  libva-dev:"${TARGETARCH}" \
  libvdpau-dev:"${TARGETARCH}" \
  libwayland-dev:"${TARGETARCH}" \
  libx11-dev:"${TARGETARCH}" \
  libxcb-shm0-dev:"${TARGETARCH}" \
  libxcb-xfixes0-dev:"${TARGETARCH}" \
  libxcb1-dev:"${TARGETARCH}" \
  libxfixes-dev:"${TARGETARCH}" \
  libxrandr-dev:"${TARGETARCH}" \
  libxtst-dev:"${TARGETARCH}" \
  nvidia-cuda-dev:"${TARGETARCH}"

rm -rf /var/lib/apt/lists/*
_DEPS

#Install Node
# hadolint ignore=SC1091
RUN <<_INSTALL_NODE
#!/bin/bash
set -e
wget -qO- https://raw.githubusercontent.com/nvm-sh/nvm/master/install.sh | bash
source "$HOME/.nvm/nvm.sh"
nvm install 20.9.0
nvm use 20.9.0
_INSTALL_NODE

# copy repository
WORKDIR /build/sunshine/
COPY --link .. .

# setup build directory
WORKDIR /build/sunshine/build

# cmake and cpack
# hadolint ignore=SC1091
RUN <<_MAKE
#!/bin/bash
set -e

# Set Node version
source "$HOME/.nvm/nvm.sh"
nvm use 20.9.0

# Configure build
cat > toolchain.cmake <<_TOOLCHAIN
set(CMAKE_ASM_COMPILER "${TUPLE}-gcc")
set(CMAKE_ASM-ATT_COMPILER "${TUPLE}-gcc")
set(CMAKE_C_COMPILER "${TUPLE}-gcc")
set(CMAKE_CXX_COMPILER "${TUPLE}-g++")
set(CMAKE_AR "${TUPLE}-gcc-ar" CACHE FILEPATH "Archive manager" FORCE)
set(CMAKE_RANLIB "${TUPLE}-gcc-ranlib" CACHE FILEPATH "Archive index generator" FORCE)
set(CMAKE_SYSTEM_PROCESSOR "$(case ${TARGETARCH} in
    armhf) echo armv7l ;;
    arm64) echo aarch64 ;;
    ppc64el) echo ppc64le ;;
    x86_64) echo amd64 ;;
    *) echo ${TARGETARCH} ;;
esac)")
set(CMAKE_SYSTEM_NAME "Linux")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
_TOOLCHAIN

export \
    PKG_CONFIG_LIBDIR=/usr/lib/"${TUPLE}"/pkgconfig:/usr/share/pkgconfig

# Actually build
cmake \
  -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake \
  -DCMAKE_CUDA_COMPILER:PATH=/usr/bin/nvcc \
  -DCMAKE_CUDA_HOST_COMPILER:PATH="${TUPLE}-g++" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE="${TARGETARCH}" \
  -DSUNSHINE_ASSETS_DIR=share/sunshine \
  -DSUNSHINE_EXECUTABLE_PATH=/usr/bin/sunshine \
  -DSUNSHINE_ENABLE_WAYLAND=ON \
  -DSUNSHINE_ENABLE_X11=ON \
  -DSUNSHINE_ENABLE_DRM=ON \
  -DSUNSHINE_ENABLE_CUDA=ON \
  /build/sunshine
make -j "$(nproc)"
cpack -G DEB
_MAKE
