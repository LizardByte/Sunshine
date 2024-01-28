# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64,linux/arm64/v8
# platforms_pr: linux/amd64,linux/arm64/v8
# no-cache-filters: sunshine-base,sunshine-install,artifacts,sunshine
ARG UNAME=lizard
ARG BASE=fedora
ARG TAG=39
FROM --platform=$BUILDPLATFORM ${BASE}:${TAG} AS sunshine-build

# reused args from base
ARG BASE
ARG TAG
ENV TAG=${TAG}

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

ENV CUDA_NATIVE_DISTRO=fedora37
ENV CUDA_CROSS_DISTRO=rhel8
ENV CUDA_RT_VERSION=12.3.101
ENV CUDA_NVCC_VERSION=12.3.107

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
# setup env
WORKDIR /env
RUN <<_ENV
#!/bin/bash
set -e

BUILD_DNF=( dnf -y --setopt=install_weak_deps=False --setopt=keepcache=True )
DNF=( "${BUILD_DNF[@]}" --releasever "${TAG}" )

case "${TARGETARCH}" in
  amd64)
    DNF_ARCH=x86_64
    CUDA_ARCH=x86_64
    DNF+=( --forcearch "${DNF_ARCH}" )
    GCC_FLAVOR=""
    ;;
  arm64)
    DNF_ARCH=aarch64
    CUDA_ARCH=sbsa
    DNF+=( --forcearch "${DNF_ARCH}" --installroot /mnt/cross )
    GCC_FLAVOR="-${DNF_ARCH}-linux-gnu"
    ;;
  ppc64le)
    DNF_ARCH=ppc64le
    CUDA_ARCH=ppc64le
    DNF+=( --forcearch "${DNF_ARCH}" --installroot /mnt/cross )
    GCC_FLAVOR="-${DNF_ARCH}-linux-gnu"
    ;;
  *)
    echo "unsupported platform: ${TARGETARCH}";
    exit 1
    ;;
esac

CUDA_REPOS="https://developer.download.nvidia.com/compute/cuda/repos"
CUDA_VERSION_SHORT="${CUDA_RT_VERSION%.*}"
TUPLE="${DNF_ARCH}-linux-gnu"

declare -p \
  BUILD_DNF \
  DNF \
  DNF_ARCH \
  CUDA_ARCH \
  CUDA_REPOS \
  CUDA_VERSION_SHORT \
  GCC_FLAVOR \
  TUPLE \
  > ./env
_ENV

# reset workdir
WORKDIR /

# install build dependencies
# hadolint ignore=DL3041
RUN --mount=type=cache,target=/var/cache/dnf,mode=0755,id=${BASE}-${TAG}-dnf <<_DEPS_A
#!/bin/bash
set -e

# shellcheck source=/dev/null
source /env/env

#curl -s -f -L --output-dir /etc/yum.repos.d -O "${CUDA_REPOS}/${CUDA_NATIVE_DISTRO}/$(uname -m)/cuda-${CUDA_NATIVE_DISTRO}.repo"

"${BUILD_DNF[@]}" -y update
"${BUILD_DNF[@]}" -y install \
  cmake-3.27.* \
  gcc"${GCC_FLAVOR}"-13.2.* \
  gcc-c++"${GCC_FLAVOR}"-13.2.* \
  git-core \
  nodejs-npm \
  pkgconf-pkg-config \
  rpm-build \
  wayland-devel \
  which
#  cuda-nvcc-"${CUDA_VERSION_SHORT//./-}" \
_DEPS_A

# install host dependencies
# hadolint ignore=DL3041
RUN --mount=type=cache,target=/mnt/cross/var/cache/dnf,mode=0755,id=${BASE}-${TAG}-dnf <<_DEPS_B
#!/bin/bash
set -e

# shellcheck source=/dev/null
source /env/env

# Initialize an array for packages
packages=(
  boost-devel-1.81.0*
  glibc-devel
  libappindicator-gtk3-devel
  libcap-devel
  libcurl-devel
  libdrm-devel
  libevdev-devel
  libnotify-devel
  libstdc++-devel
  libva-devel
  libvdpau-devel
  libX11-devel
  libxcb-devel
  libXcursor-devel
  libXfixes-devel
  libXi-devel
  libXinerama-devel
  libXrandr-devel
  libXtst-devel
  mesa-libGL-devel
  miniupnpc-devel
  numactl-devel
  openssl-devel
  opus-devel
  pulseaudio-libs-devel
  wayland-devel
)

# Conditionally include arch specific packages
if [[ "${TARGETARCH}" == 'amd64' ]]; then
   packages+=(intel-mediasdk-devel)
fi

"${DNF[@]}" install \
  filesystem

# Install packages using the array
"${DNF[@]}" --setopt=tsflags=noscripts install "${packages[@]}"

# if [[ "${BUILDARCH}" != "${TARGETARCH}" ]]; then
#   for URL in "${CUDA_REPOS}/${CUDA_CROSS_DISTRO}/${CUDA_ARCH}"/{cuda-cudart-devel-${CUDA_VERSION_SHORT//./-}-${CUDA_RT_VERSION},cuda-nvcc-${CUDA_VERSION_SHORT//./-}-${CUDA_NVCC_VERSION}}-1.${DNF_ARCH}.rpm; do
#     curl -s -f -L "${URL}" | rpm2archive | tar --directory=/ -zx "./usr/local/cuda-${CUDA_VERSION_SHORT}/targets/"
#   done
# fi
_DEPS_B

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

if [[ "${TARGETARCH}" != 'amd64' ]]; then
  cat >> toolchain.cmake <<_TOOLCHAIN
    set(CMAKE_SYSROOT "/mnt/cross")
_TOOLCHAIN

  CXX_FLAG_1="$(echo /mnt/cross/usr/include/c++/[0-9]*/)"
  CXX_FLAG_2="$(echo /mnt/cross/usr/include/c++/[0-9]*/${TUPLE%%-*}-*/)"
  LD_FLAG="$(echo /mnt/cross/usr/lib/gcc/${TUPLE%%-*}-*/[0-9]*/)"

  export \
    CXXFLAGS="-isystem ${CXX_FLAG_1} -isystem ${CXX_FLAG_2}" \
    LDFLAGS="--sysroot=/mnt/cross -L${LD_FLAG}" \
    PKG_CONFIG_LIBDIR=/mnt/cross/usr/lib64/pkgconfig:/mnt/cross/usr/share/pkgconfig \
    PKG_CONFIG_SYSROOT_DIR=/mnt/cross \
    PKG_CONFIG_SYSTEM_INCLUDE_PATH=/mnt/cross/usr/include \
    PKG_CONFIG_SYSTEM_LIBRARY_PATH=/mnt/cross/usr/lib64

  export \
    CUDAFLAGS="--compiler-options --sysroot=/mnt/cross,${CXXFLAGS// /,} --linker-options ${LDFLAGS// /,}"
fi

cmake \
  -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake \
  -DCMAKE_CUDA_COMPILER:PATH="/usr/local/cuda-${CUDA_VERSION_SHORT}/bin/nvcc;${CUDAFLAGS// /;}" \
  -DCMAKE_CUDA_HOST_COMPILER:PATH="${TUPLE}-g++" \
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

FROM ${BASE}:${TAG} AS sunshine-base

FROM --platform=$BUILDPLATFORM ${BASE}:${TAG} AS sunshine-install
ARG BASE
ARG TAG
ARG TARGETARCH

COPY --link --from=sunshine-build /env/env /env/env
COPY --link --from=sunshine-base /usr/lib/sysimage/rpm /mnt/cross/usr/lib/sysimage/rpm
COPY --link --from=sunshine-base /etc/passwd /etc/group /etc/shadow /etc/gshadow /etc/sub?id /mnt/cross/etc/

# setup user
ARG PGID=1000
ENV PGID=${PGID}
ARG PUID=1000
ENV PUID=${PUID}
ARG UNAME
ENV UNAME=${UNAME}

# install sunshine
RUN --mount=type=cache,target=/mnt/cross/var/cache/dnf,mode=0755,id=${BASE}-${TAG}-dnf \
    --mount=type=bind,from=artifacts,source=/sunshine-${BASE}-${TAG}-${TARGETARCH}.rpm,target=/tmp/sunshine.rpm \
    <<_INSTALL_SUNSHINE
#!/bin/bash
set -e

# shellcheck source=/dev/null
source /env/env

"${DNF[@]}" reinstall filesystem
"${DNF[@]}" --setopt=tsflags=noscripts update
"${DNF[@]}" --setopt=tsflags=noscripts install /tmp/sunshine.rpm

groupadd -R /mnt/cross -f -g "${PGID}" "${UNAME}"
useradd -R /mnt/cross -lm -d "/home/${UNAME}" -s /bin/bash -g "${PGID}" -u "${PUID}" "${UNAME}"
mkdir -p "/mnt/cross/home/${UNAME}/.config/sunshine"
ln -s "home/${UNAME}/.config/sunshine" /mnt/cross/config
chown -R "${PUID}:${PGID}" "/mnt/cross/home/${UNAME}"
_INSTALL_SUNSHINE

FROM ${BASE}:${TAG} AS sunshine
COPY --link --from=sunshine-install /mnt/cross /

# network setup
EXPOSE 47984-47990/tcp
EXPOSE 48010
EXPOSE 47998-48000/udp

ARG UNAME
ENV HOME=/home/${UNAME}
ENV TZ="UTC"

USER ${UNAME}
WORKDIR ${HOME}

# entrypoint
ENTRYPOINT ["/usr/bin/sunshine"]
