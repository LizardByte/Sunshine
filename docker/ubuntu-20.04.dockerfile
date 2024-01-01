# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64,linux/arm64/v8
# platforms_pr: linux/amd64
# no-cache-filters: sunshine-base,artifacts,sunshine
ARG BASE=ubuntu
ARG TAG=20.04
FROM ${BASE}:${TAG} AS sunshine-base

ENV DEBIAN_FRONTEND=noninteractive

FROM sunshine-base as sunshine-build

ARG TARGETPLATFORM
RUN echo "target_platform: ${TARGETPLATFORM}"

ARG BRANCH
ARG BUILD_VERSION
ARG COMMIT
# note: BUILD_VERSION may be blank

ENV BRANCH=${BRANCH}
ENV BUILD_VERSION=${BUILD_VERSION}
ENV COMMIT=${COMMIT}

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
# install dependencies
RUN <<_DEPS
#!/bin/bash
set -e
apt-get update -y
apt-get install -y --no-install-recommends \
  build-essential \
  ca-certificates \
  gcc-10=10.5.* \
  g++-10=10.5.* \
  git \
  libayatana-appindicator3-dev \
  libavdevice-dev \
  libboost-filesystem-dev=1.71.* \
  libboost-locale-dev=1.71.* \
  libboost-log-dev=1.71.* \
  libboost-program-options-dev=1.71.* \
  libcap-dev \
  libcurl4-openssl-dev \
  libdrm-dev \
  libevdev-dev \
  libminiupnpc-dev \
  libnotify-dev \
  libnuma-dev \
  libopus-dev \
  libpulse-dev \
  libssl-dev \
  libva-dev \
  libvdpau-dev \
  libwayland-dev \
  libx11-dev \
  libxcb-shm0-dev \
  libxcb-xfixes0-dev \
  libxcb1-dev \
  libxfixes-dev \
  libxrandr-dev \
  libxtst-dev \
  wget
if [[ "${TARGETPLATFORM}" == 'linux/amd64' ]]; then
  apt-get install -y --no-install-recommends \
    libmfx-dev
fi
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

# Update gcc alias
# https://stackoverflow.com/a/70653945/11214013
RUN <<_GCC_ALIAS
#!/bin/bash
set -e
update-alternatives --install \
  /usr/bin/gcc gcc /usr/bin/gcc-10 100 \
  --slave /usr/bin/g++ g++ /usr/bin/g++-10 \
  --slave /usr/bin/gcov gcov /usr/bin/gcov-10 \
  --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-10 \
  --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-10
_GCC_ALIAS

# install cmake
# sunshine requires cmake >= 3.18
WORKDIR /build/cmake
# https://cmake.org/download/
ENV CMAKE_VERSION="3.25.1"
# hadolint ignore=SC3010
RUN <<_INSTALL_CMAKE
#!/bin/bash
set -e
cmake_prefix="https://github.com/Kitware/CMake/releases/download/v"
if [[ "${TARGETPLATFORM}" == 'linux/amd64' ]]; then
  cmake_arch="x86_64"
elif [[ "${TARGETPLATFORM}" == 'linux/arm64' ]]; then
  cmake_arch="aarch64"
fi
url="${cmake_prefix}${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${cmake_arch}.sh"
echo "cmake url: ${url}"
wget "$url" --progress=bar:force:noscroll -q --show-progress -O ./cmake.sh
sh ./cmake.sh --prefix=/usr/local --skip-license
rm ./cmake.sh
_INSTALL_CMAKE

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
if [[ "${TARGETPLATFORM}" == 'linux/arm64' ]]; then
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
cmake \
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

FROM sunshine-base as sunshine

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
