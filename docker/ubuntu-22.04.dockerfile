# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64,linux/arm64/v8
# platforms_pr: linux/amd64,linux/arm64/v8
# no-cache-filters: artifacts,sunshine
ARG BASE=ubuntu
ARG TAG=22.04
ARG DIST=jammy
FROM --platform=$BUILDPLATFORM ${BASE}:${TAG} AS sunshine-build

ENV DEBIAN_FRONTEND=noninteractive

# reused args from base
ARG BASE
ARG TAG
ENV TAG=${TAG}
ARG DIST
ENV DIST=${DIST}

ARG BUILDARCH
ARG TARGETARCH
RUN echo "build_arch: ${BUILDARCH}"
RUN echo "target_arch: ${TARGETARCH}"

# args from ci workflow
ARG BRANCH
ARG BUILD_VERSION
ARG COMMIT
# note: BUILD_VERSION may be blank

ENV BRANCH=${BRANCH}
ENV BUILD_VERSION=${BUILD_VERSION}
ENV COMMIT=${COMMIT}

ENV CUDA_DISTRO=rhel8
ENV CUDA_RT_VERSION=12.3.101
ENV CUDA_NVCC_VERSION=12.3.107

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
# install dependencies
WORKDIR /env
RUN --mount=type=cache,target=/var/cache/apt/archives,mode=0755,id=${BASE}-${TAG}-apt-archives \
    --mount=type=cache,target=/var/lib/apt/lists,mode=0755,id=${BASE}-${TAG}-apt-lists <<_DEPS
#!/bin/bash
set -e

# Keep downloaded archives in the cache.
rm /etc/apt/apt.conf.d/docker-clean

case "${TARGETARCH}" in
  amd64)
    DEB_ARCH=amd64
    DNF_ARCH=x86_64
    CUDA_ARCH=x86_64
    TUPLE=x86_64-linux-gnu
    ;;
  arm64)
    DEB_ARCH=arm64
    DNF_ARCH=aarch64
    CUDA_ARCH=sbsa
    TUPLE=aarch64-linux-gnu
    ;;
  ppc64le)
    DEB_ARCH=ppc64el
    DNF_ARCH=ppc64le
    CUDA_ARCH=ppc64le
    TUPLE=powerpc64le-linux-gnu
    ;;
  *)
    echo "unsupported arch: ${TARGETARCH}";
    exit 1
    ;;
esac

declare -p DEB_ARCH DNF_ARCH TUPLE > env

apt-get update -y
apt-get install -y --no-install-recommends \
  apt-transport-https \
  ca-certificates \
  gnupg \
  wget

source /etc/lsb-release

CUDA_REPOS="https://developer.download.nvidia.com/compute/cuda/repos"
CUDA_UBUNTU="${CUDA_REPOS}/ubuntu${DISTRIB_RELEASE//.}/$(uname -m)"
CUDA_VERSION_SHORT="${CUDA_RT_VERSION%.*}"

wget -qO- "${CUDA_UBUNTU}/3bf863cc.pub" | gpg --dearmor > /etc/apt/trusted.gpg.d/cuda.gpg
cat > /etc/apt/sources.list.d/cuda.list <<_SOURCES
  deb [arch=$(dpkg --print-architecture)] ${CUDA_UBUNTU}/ /
_SOURCES

if [[ "${BUILDARCH}" != "${TARGETARCH}" ]]; then
  sed -i "s/^deb /deb [arch=$(dpkg --print-architecture)] /" /etc/apt/sources.list
  dpkg --add-architecture "${DEB_ARCH}"

  cat > /etc/apt/sources.list.d/ports.list <<_SOURCES
    deb [arch=${DEB_ARCH}] http://ports.ubuntu.com/ubuntu-ports/ ${DISTRIB_CODENAME} main restricted universe multiverse
    deb [arch=${DEB_ARCH}] http://ports.ubuntu.com/ubuntu-ports/ ${DISTRIB_CODENAME}-updates main restricted universe multiverse
_SOURCES
fi

# Initialize an array for packages
packages=(
  "cmake=3.22.*"
  "cuda-nvcc-${CUDA_VERSION_SHORT//./-}"
  "git"
  "libwayland-bin"
  "pkgconf"
  "rpm2cpio"
  "libayatana-appindicator3-dev:${DEB_ARCH}"
  "libavdevice-dev:${DEB_ARCH}"
  "libboost-filesystem-dev:${DEB_ARCH}=1.74.*"
  "libboost-locale-dev:${DEB_ARCH}=1.74.*"
  "libboost-log-dev:${DEB_ARCH}=1.74.*"
  "libboost-program-options-dev:${DEB_ARCH}=1.74.*"
  "libcap-dev:${DEB_ARCH}"
  "libcurl4-openssl-dev:${DEB_ARCH}"
  "libdrm-dev:${DEB_ARCH}"
  "libevdev-dev:${DEB_ARCH}"
  "libminiupnpc-dev:${DEB_ARCH}"
  "libnotify-dev:${DEB_ARCH}"
  "libnuma-dev:${DEB_ARCH}"
  "libopus-dev:${DEB_ARCH}"
  "libpulse-dev:${DEB_ARCH}"
  "libssl-dev:${DEB_ARCH}"
  "libva-dev:${DEB_ARCH}"
  "libvdpau-dev:${DEB_ARCH}"
  "libwayland-dev:${DEB_ARCH}"
  "libx11-dev:${DEB_ARCH}"
  "libxcb-shm0-dev:${DEB_ARCH}"
  "libxcb-xfixes0-dev:${DEB_ARCH}"
  "libxcb1-dev:${DEB_ARCH}"
  "libxfixes-dev:${DEB_ARCH}"
  "libxrandr-dev:${DEB_ARCH}"
  "libxtst-dev:${DEB_ARCH}"
)

# Conditionally include arch specific packages
if [[ "${TARGETARCH}" == 'amd64' ]]; then
  packages+=(
    "libmfx-dev:${DEB_ARCH}"
  )
fi
if [[ "${BUILDARCH}" == "${TARGETARCH}" ]]; then
  packages+=(
    "g++=4:11.2.*"
    "gcc=4:11.2.*"
  )
else
  packages+=(
    "g++-${TUPLE}=4:11.2.*"
    "gcc-${TUPLE}=4:11.2.*"
  )
fi

apt-get update -y
apt-get install -y --no-install-recommends "${packages[@]}"

if [[ "${BUILDARCH}" != "${TARGETARCH}" ]]; then
  for URL in "${CUDA_REPOS}/${CUDA_DISTRO}/${CUDA_ARCH}"/{cuda-cudart-devel-${CUDA_VERSION_SHORT//./-}-${CUDA_RT_VERSION},cuda-nvcc-${CUDA_VERSION_SHORT//./-}-${CUDA_NVCC_VERSION}}-1.${DNF_ARCH}.rpm; do
    wget -q "${URL}"
    rpm2archive "${URL##*/}"
    tar --directory=/ -zxf "${URL##*/}.tgz" "./usr/local/cuda-${CUDA_VERSION_SHORT}/targets/"
  done
fi
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
#Set Node version
source "$HOME/.nvm/nvm.sh"
nvm use 20.9.0

# shellcheck source=/dev/null
source /env/env

# Configure build
cat > toolchain.cmake <<_TOOLCHAIN
set(CMAKE_ASM_COMPILER "${TUPLE}-gcc")
set(CMAKE_ASM-ATT_COMPILER "${TUPLE}-gcc")
set(CMAKE_C_COMPILER "${TUPLE}-gcc")
set(CMAKE_CXX_COMPILER "${TUPLE}-g++")
set(CMAKE_AR "${TUPLE}-gcc-ar" CACHE FILEPATH "Archive manager" FORCE)
set(CMAKE_RANLIB "${TUPLE}-gcc-ranlib" CACHE FILEPATH "Archive index generator" FORCE)
set(CMAKE_SYSTEM_PROCESSOR "$(case ${TARGETARCH} in
    arm) echo armv7l ;;
    *) echo ${DNF_ARCH} ;;
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
  -DCMAKE_CUDA_COMPILER:PATH=/usr/local/cuda/bin/nvcc \
  -DCMAKE_CUDA_HOST_COMPILER:PATH="${TUPLE}-g++" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE="${DEB_ARCH}" \
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

FROM scratch AS artifacts
ARG BASE
ARG TAG
ARG TARGETARCH
COPY --link --from=sunshine-build /build/sunshine/build/cpack_artifacts/Sunshine.deb /sunshine-${BASE}-${TAG}-${TARGETARCH}.deb

FROM ${BASE}:${TAG} as sunshine
ARG BASE
ARG TAG
ARG TARGETARCH

# install sunshine
RUN --mount=type=cache,target=/var/cache/apt/archives,mode=0755,id=${BASE}-${TAG}-apt-archives \
    --mount=type=cache,target=/var/lib/apt/lists,mode=0755,id=${BASE}-${TAG}-apt-lists \
    --mount=type=bind,from=artifacts,source=/sunshine-${BASE}-${TAG}-${TARGETARCH}.deb,target=/tmp/sunshine.deb \
    <<_INSTALL_SUNSHINE
#!/bin/bash
set -e
apt-get update -y
apt-get install -y --no-install-recommends /tmp/sunshine.deb
_INSTALL_SUNSHINE

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

# setup user
RUN <<_SETUP_USER
#!/bin/bash
set -e
groupadd -f -g "${PGID}" "${UNAME}"
useradd -lm -d ${HOME} -s /bin/bash -g "${PGID}" -u "${PUID}" "${UNAME}"
mkdir -p ${HOME}/.config/sunshine
ln -s ${HOME}/.config/sunshine /config
chown -R ${UNAME} ${HOME}
_SETUP_USER

USER ${UNAME}
WORKDIR ${HOME}

# entrypoint
ENTRYPOINT ["/usr/bin/sunshine"]
