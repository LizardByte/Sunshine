# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64,linux/arm64/v8
# platforms_pr: linux/amd64,linux/arm64/v8
# no-cache-filters: sunshine-base,artifacts,sunshine
ARG BASE=fedora
ARG TAG=39
FROM --platform=$BUILDPLATFORM ${BASE}:${TAG} AS sunshine-base

FROM sunshine-base as sunshine-build

# reused args from base
ARG TAG
ENV TAG=${TAG}

ARG TARGETPLATFORM
RUN echo "target_platform: ${TARGETPLATFORM}"

# args from ci workflow
ARG BRANCH
ARG BUILD_VERSION
ARG COMMIT
# note: BUILD_VERSION may be blank

ENV BRANCH=${BRANCH}
ENV BUILD_VERSION=${BUILD_VERSION}
ENV COMMIT=${COMMIT}

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
# setup env
WORKDIR /env
RUN <<_ENV
#!/bin/bash
set -e
case "${TARGETPLATFORM}" in
  linux/amd64)
    echo TARGETARCH=x86_64 >> ./env
    echo TUPLE=x86_64-linux-gnu >> ./env
    ;;
  linux/arm64)
    echo TARGETARCH=aarch64 >> ./env
    echo TUPLE=aarch64-linux-gnu >> ./env
    ;;
  *)
    echo "unsupported platform: ${TARGETPLATFORM}";
    exit 1
    ;;
esac
_ENV

# reset workdir
WORKDIR /

# install build dependencies
# hadolint ignore=DL3041
RUN <<_DEPS_A
#!/bin/bash
set -e

# shellcheck source=/dev/null
source /env/env

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
_DEPS_A

# install host dependencies
# hadolint ignore=DL3041
RUN <<_DEPS_B
#!/bin/bash
set -e

# shellcheck source=/dev/null
source /env/env

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
_DEPS_B

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
#
## shellcheck source=/dev/null
#source /env/env
#cuda_prefix="https://developer.download.nvidia.com/compute/cuda/"
#cuda_suffix=""
#if [[ "${TARGETARCH}" == 'aarch64' ]]; then
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

# shellcheck source=/dev/null
source /env/env

export \
  CXXFLAGS="-isystem $(echo /mnt/cross/usr/include/c++/[0-9]*/) -isystem $(echo /mnt/cross/usr/include/c++/[0-9]*/${TUPLE%%-*}-*/)" \
  LDFLAGS="-L$(echo /mnt/cross/usr/lib/gcc/${TUPLE%%-*}-*/[0-9]*/)" \
  PKG_CONFIG_LIBDIR=/mnt/cross/usr/lib64/pkgconfig:/mnt/cross/usr/share/pkgconfig \
  PKG_CONFIG_SYSROOT_DIR=/mnt/cross \
  PKG_CONFIG_SYSTEM_INCLUDE_PATH=/mnt/cross/usr/include \
  PKG_CONFIG_SYSTEM_LIBRARY_PATH=/mnt/cross/usr/lib64

cmake \
  -DCMAKE_TOOLCHAIN_FILE=toolchain-${TARGETARCH}-linux-gnu.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
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

FROM scratch AS artifacts
ARG BASE
ARG TAG
ARG TARGETARCH
COPY --link --from=sunshine-build /build/sunshine/build/cpack_artifacts/Sunshine.rpm /sunshine-${BASE}-${TAG}-${TARGETARCH}.rpm

FROM ${BASE}:${TAG} AS sunshine

# copy deb from builder
COPY --link --from=artifacts /sunshine*.rpm /sunshine.rpm

# install sunshine
RUN <<_INSTALL_SUNSHINE
#!/bin/bash
set -e
dnf -y update
dnf -y install /sunshine.rpm
dnf clean all
rm -rf /var/cache/yum
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
