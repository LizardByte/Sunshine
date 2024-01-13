# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64,linux/arm64/v8
# platforms_pr: linux/amd64
# no-cache-filters: sunshine-base,artifacts,sunshine
ARG BASE=fedora
ARG TAG=39
FROM ${BASE}:${TAG} AS sunshine-base

FROM sunshine-base as sunshine-build

ARG TAG
ARG TARGETARCH=aarch64
ARG TUPLE=${TARGETARCH}-linux-gnu

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

# install build dependencies
# hadolint ignore=DL3041
RUN <<_DEPS
#!/bin/bash
set -e
dnf -y update
dnf -y install \
  cmake-3.27.* \
  gcc-"${TARGETARCH}"-linux-gnu-13.2.* \
  gcc-c++-"${TARGETARCH}"-linux-gnu-13.2.* \
  git-core \
  nodejs \
  pkgconf-pkg-config \
  rpm-build \
  wayland-devel \
  wget \
  which
dnf clean all
_DEPS

# install host dependencies
# hadolint ignore=DL3041
RUN <<_DEPS
#!/bin/bash
set -e
DNF=( dnf -y --installroot /mnt/cross --releasever "${TAG}" --forcearch "${TARGETARCH}" )
"${DNF[@]}" install \
  filesystem
"${DNF[@]}" --setopt=tsflags=noscripts install \
  $([[ "${TARGETARCH}" == x86_64 ]] && echo intel-mediasdk-devel) \
  boost-devel-1.81.0* \
  glibc-devel \
  libappindicator-gtk3-devel \
  libcap-devel \
  libcurl-devel \
  libdrm-devel \
  libevdev-devel \
  libnotify-devel \
  libstdc++-devel \
  libva-devel \
  libvdpau-devel \
  libX11-devel \
  libxcb-devel \
  libXcursor-devel \
  libXfixes-devel \
  libXi-devel \
  libXinerama-devel \
  libXrandr-devel \
  libXtst-devel \
  mesa-libGL-devel \
  miniupnpc-devel \
  numactl-devel \
  openssl-devel \
  opus-devel \
  pulseaudio-libs-devel \
  wayland-devel
"${DNF[@]}" clean all
_DEPS

# todo - enable cuda once it's supported for gcc 13 and fedora 39
## install cuda
#WORKDIR /build/cuda
## versions: https://developer.nvidia.com/cuda-toolkit-archive
#ENV CUDA_VERSION="12.0.0"
#ENV CUDA_BUILD="525.60.13"
## hadolint ignore=SC3010
#RUN <<_INSTALL_CUDA
##!/bin/bash
#set -e
#cuda_prefix="https://developer.download.nvidia.com/compute/cuda/"
#cuda_suffix=""
#if [[ "${TARGETARCH}" == aarch64 ]]; then
#  cuda_suffix="_sbsa"
#fi
#url="${cuda_prefix}${CUDA_VERSION}/local_installers/cuda_${CUDA_VERSION}_${CUDA_BUILD}_linux${cuda_suffix}.run"
#echo "cuda url: ${url}"
#wget "$url" --progress=bar:force:noscroll -q --show-progress -O ./cuda.run
#chmod a+x ./cuda.run
#./cuda.run --silent --toolkit --toolkitpath=/build/cuda --no-opengl-libs --no-man-page --no-drm
#rm ./cuda.run
#_INSTALL_CUDA

# copy repository
WORKDIR /build/sunshine/
COPY --link .. .

# setup build directory
WORKDIR /build/sunshine/build

# cmake and cpack
# todo - add cmake argument back in for cuda support "-DCMAKE_CUDA_COMPILER:PATH=/build/cuda/bin/nvcc \"
# todo - re-enable "DSUNSHINE_ENABLE_CUDA"
RUN <<_MAKE
#!/bin/bash
set -e

cat > toolchain.cmake <<_TOOLCHAIN
set(CMAKE_ASM_COMPILER "${TUPLE}-gcc")
set(CMAKE_ASM-ATT_COMPILER "${TUPLE}-gcc")
set(CMAKE_C_COMPILER "${TUPLE}-gcc")
set(CMAKE_CXX_COMPILER "${TUPLE}-g++")
set(CMAKE_AR "${TUPLE}-gcc-ar" CACHE FILEPATH "Archive manager" FORCE)
set(CMAKE_RANLIB "${TUPLE}-gcc-ranlib" CACHE FILEPATH "Archive index generator" FORCE)
set(CMAKE_SYSTEM_PROCESSOR "${TARGETARCH}")
set(CMAKE_SYSTEM_NAME "Linux")
set(CMAKE_SYSROOT "/mnt/cross")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
_TOOLCHAIN

export \
    CXXFLAGS="-isystem $(echo /mnt/cross/usr/include/c++/[0-9]*/) -isystem $(echo /mnt/cross/usr/include/c++/[0-9]*/${TUPLE%%-*}-*/)" \
    LDFLAGS="-L$(echo /mnt/cross/usr/lib/gcc/${TUPLE%%-*}-*/[0-9]*/)" \
    PKG_CONFIG_LIBDIR=/mnt/cross/usr/lib64/pkgconfig:/mnt/cross/usr/share/pkgconfig \
    PKG_CONFIG_SYSROOT_DIR=/mnt/cross \
    PKG_CONFIG_SYSTEM_INCLUDE_PATH=/mnt/cross/usr/include \
    PKG_CONFIG_SYSTEM_LIBRARY_PATH=/mnt/cross/usr/lib64

cmake \
  -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCPACK_RPM_PACKAGE_ARCHITECTURE="${TARGETARCH}" \
  -DSUNSHINE_ASSETS_DIR=share/sunshine \
  -DSUNSHINE_EXECUTABLE_PATH=/usr/bin/sunshine \
  -DSUNSHINE_ENABLE_WAYLAND=ON \
  -DSUNSHINE_ENABLE_X11=ON \
  -DSUNSHINE_ENABLE_DRM=ON \
  -DSUNSHINE_ENABLE_CUDA=OFF \
  /build/sunshine
make -j "$(nproc)"
cpack -G RPM
_MAKE
