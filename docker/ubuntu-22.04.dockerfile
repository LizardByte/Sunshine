# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64,linux/arm64/v8
# platforms_pr: linux/amd64,linux/arm64/v8
# no-cache-filters: sunshine-base,artifacts,sunshine
ARG BASE=ubuntu
ARG TAG=22.04
ARG DIST=jammy
FROM --platform=$BUILDPLATFORM ${BASE}:${TAG} AS sunshine-base

ENV DEBIAN_FRONTEND=noninteractive

FROM sunshine-base as sunshine-build

# reused args from base
ARG TAG
ENV TAG=${TAG}
ARG DIST
ENV DIST=${DIST}

ARG BUILDPLATFORM
ARG TARGETPLATFORM
RUN echo "build_platform: ${BUILDPLATFORM}"
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
case "${BUILDPLATFORM}" in
  linux/amd64)
    BUILDARCH=amd64
    ;;
  linux/arm64)
    BUILDARCH=arm64
    ;;
  *)
    echo "unsupported platform: ${TARGETPLATFORM}";
    exit 1
    ;;
esac

case "${TARGETPLATFORM}" in
  linux/amd64)
    PACKAGEARCH=amd64
    TARGETARCH=x86_64
    ;;
  linux/arm64)
    PACKAGEARCH=arm64
    TARGETARCH=aarch64
    ;;
  *)
    echo "unsupported platform: ${TARGETPLATFORM}";
    exit 1
    ;;
esac

mirror="http://ports.ubuntu.com/ubuntu-ports"
extra_sources=$(cat <<- VAREOF
  deb [arch=$PACKAGEARCH] $mirror $DIST main restricted
  deb [arch=$PACKAGEARCH] $mirror $DIST-updates main restricted
  deb [arch=$PACKAGEARCH] $mirror $DIST universe
  deb [arch=$PACKAGEARCH] $mirror $DIST-updates universe
  deb [arch=$PACKAGEARCH] $mirror $DIST multiverse
  deb [arch=$PACKAGEARCH] $mirror $DIST-updates multiverse
  deb [arch=$PACKAGEARCH] $mirror $DIST-backports main restricted universe multiverse
  deb [arch=$PACKAGEARCH] $mirror $DIST-security main restricted
  deb [arch=$PACKAGEARCH] $mirror $DIST-security universe
  deb [arch=$PACKAGEARCH] $mirror $DIST-security multiverse
VAREOF
)

if [[ "${BUILDPLATFORM}" != "${TARGETPLATFORM}" ]]; then
  # fix original sources
  sed -i -e "s#deb http#deb [arch=$BUILDARCH] http#g" /etc/apt/sources.list
  dpkg --add-architecture $PACKAGEARCH

  echo "$extra_sources" | tee -a /etc/apt/sources.list
fi

echo PACKAGEARCH=${PACKAGEARCH}; \
echo TARGETARCH=${TARGETARCH}; \
echo TUPLE=${TARGETARCH}-linux-gnu >> ./env
_ENV

# install dependencies
RUN <<_DEPS
#!/bin/bash
set -e

# shellcheck source=/dev/null
source /env/env

# Initialize an array for packages
packages=(
  "build-essential"
  "cmake=3.22.*"
  "ca-certificates"
  "git"
  "libayatana-appindicator3-dev"
  "libavdevice-dev"
  "libboost-filesystem-dev=1.74.*"
  "libboost-locale-dev=1.74.*"
  "libboost-log-dev=1.74.*"
  "libboost-program-options-dev=1.74.*"
  "libcap-dev"
  "libcurl4-openssl-dev"
  "libdrm-dev"
  "libevdev-dev"
  "libminiupnpc-dev"
  "libnotify-dev"
  "libnuma-dev"
  "libopus-dev"
  "libpulse-dev"
  "libssl-dev"
  "libva-dev"
  "libvdpau-dev"
  "libwayland-dev"
  "libx11-dev"
  "libxcb-shm0-dev"
  "libxcb-xfixes0-dev"
  "libxcb1-dev"
  "libxfixes-dev"
  "libxrandr-dev"
  "libxtst-dev"
  "wget"
)

# Conditionally include arch specific packages
if [[ "${TARGETARCH}" == 'x86_64' ]]; then
  packages+=(
    "libmfx-dev"
  )
fi
if [[ "${BUILDPLATFORM}" != "${TARGETPLATFORM}" ]]; then
  packages+=(
    "g++-${TUPLE}=4:11.2.*"
    "gcc-${TUPLE}=4:11.2.*"
  )
fi

apt-get update -y
apt-get install -y --no-install-recommends "${packages[@]}"
apt-get clean
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

# install cuda
WORKDIR /build/cuda
# versions: https://developer.nvidia.com/cuda-toolkit-archive
ENV CUDA_VERSION="11.8.0"
ENV CUDA_BUILD="520.61.05"
# hadolint ignore=SC3010
RUN <<_INSTALL_CUDA
#!/bin/bash
set -e
cuda_prefix="https://developer.download.nvidia.com/compute/cuda/"
cuda_suffix=""
if [[ "${TARGETARCH}" == 'aarch64' ]]; then
  cuda_suffix="_sbsa"
fi
url="${cuda_prefix}${CUDA_VERSION}/local_installers/cuda_${CUDA_VERSION}_${CUDA_BUILD}_linux${cuda_suffix}.run"
echo "cuda url: ${url}"
wget "$url" --progress=bar:force:noscroll -q --show-progress -O ./cuda.run
chmod a+x ./cuda.run
./cuda.run --silent --toolkit --toolkitpath=/build/cuda --no-opengl-libs --no-man-page --no-drm
rm ./cuda.run
_INSTALL_CUDA

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

TOOLCHAIN_OPTION=""
if [[ "${BUILDPLATFORM}" != "${TARGETPLATFORM}" ]]; then
  export "CCPREFIX=/usr/bin/${TUPLE}-"

  TOOLCHAIN_OPTION="-DCMAKE_TOOLCHAIN_FILE=toolchain-${TUPLE}-debian.cmake"
fi

#Actually build
cmake \
  "$TOOLCHAIN_OPTION" \
  -DCMAKE_CUDA_COMPILER:PATH=/build/cuda/bin/nvcc \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
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

# copy deb from builder
COPY --link --from=artifacts /sunshine*.deb /sunshine.deb

# install sunshine
RUN <<_INSTALL_SUNSHINE
#!/bin/bash
set -e
apt-get update -y
apt-get install -y --no-install-recommends /sunshine.deb
apt-get clean
rm -rf /var/lib/apt/lists/*
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
