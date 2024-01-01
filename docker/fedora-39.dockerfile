# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64,linux/arm64/v8
# platforms_pr: linux/amd64
# no-cache-filters: sunshine-base,artifacts,sunshine
ARG BASE=fedora
ARG TAG=39
FROM ${BASE}:${TAG} AS sunshine-base

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
# hadolint ignore=DL3041
RUN <<_DEPS
#!/bin/bash
set -e
dnf -y update
dnf -y group install "Development Tools"
dnf -y install \
  boost-devel-1.81.0* \
  cmake-3.27.* \
  gcc-13.2.* \
  gcc-c++-13.2.* \
  git \
  libappindicator-gtk3-devel \
  libcap-devel \
  libcurl-devel \
  libdrm-devel \
  libevdev-devel \
  libnotify-devel \
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
  nodejs \
  numactl-devel \
  openssl-devel \
  opus-devel \
  pulseaudio-libs-devel \
  rpm-build \
  wget \
  which
if [[ "${TARGETPLATFORM}" == 'linux/amd64' ]]; then
  dnf -y install intel-mediasdk-devel
fi
dnf clean all
rm -rf /var/cache/yum
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
#if [[ "${TARGETPLATFORM}" == 'linux/arm64' ]]; then
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
cmake \
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

FROM sunshine-base as sunshine

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
