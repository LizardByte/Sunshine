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
  cmake=3.22.* \
  git \
  libappindicator3-dev \
  libavdevice-dev \
  libboost-filesystem-dev=1.74.* \
  libboost-locale-dev=1.74.* \
  libboost-log-dev=1.74.* \
  libboost-program-options-dev=1.74.* \
  libboost-thread-dev=1.74.* \
  libcap-dev \
  libcurl4-openssl-dev \
  libdrm-dev \
  libevdev-dev \
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
  nodejs \
  npm \
  wget
if [[ "${TARGETPLATFORM}" == 'linux/amd64' ]]; then
  apt-get install -y --no-install-recommends \
    libmfx-dev
fi
apt-get clean
rm -rf /var/lib/apt/lists/*
_DEPS

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

# setup npm dependencies
RUN npm install

# setup build directory
WORKDIR /build/sunshine/build

# cmake and cpack
RUN <<_MAKE
#!/bin/bash
set -e
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
