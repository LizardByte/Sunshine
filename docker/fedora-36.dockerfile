# syntax=docker/dockerfile:1.4
# artifacts: true
# platforms: linux/amd64,linux/arm64/v8
# platforms_pr: linux/amd64
ARG BASE=fedora
ARG TAG=36
FROM ${BASE}:${TAG} AS sunshine-base

FROM sunshine-base as sunshine-build

ARG TARGETPLATFORM
RUN echo "target_platform: ${TARGETPLATFORM}"

SHELL ["/bin/bash", "-o", "pipefail", "-c"]
# install dependencies
# hadolint ignore=DL3041
RUN <<_DEPS
#!/bin/bash
dnf -y update
dnf -y group install "Development Tools"
dnf -y install \
  boost-devel-1.76.0* \
  boost-static-1.76.0* \
  cmake-3.22.2* \
  gcc-12.0.1* \
  gcc-c++-12.0.1* \
  libcap-devel-2.48* \
  libcurl-devel-7.82.0* \
  libdrm-devel-2.4.110* \
  libevdev-devel-1.12.0* \
  libva-devel-2.14.0* \
  libvdpau-devel-1.5* \
  libX11-devel-1.7.3* \
  libxcb-devel-1.13.1* \
  libXcursor-devel-1.2.0* \
  libXfixes-devel-6.0.0* \
  libXi-devel-1.8* \
  libXinerama-devel-1.1.4* \
  libXrandr-devel-1.5.2* \
  libXtst-devel-1.2.3* \
  mesa-libGL-devel-22.0.1* \
  npm-8.3.1* \
  numactl-devel-2.0.14* \
  openssl-devel-3.0.2* \
  opus-devel-1.3.1* \
  pulseaudio-libs-devel-15.0* \
  rpm-build-4.17.0* \
  wget-1.21.3* \
  which-2.21*
if [[ "${TARGETPLATFORM}" == 'linux/amd64' ]]; then
  dnf -y install intel-mediasdk-devel-22.3.0*
fi
dnf clean all
rm -rf /var/cache/yum
_DEPS

# install cuda
WORKDIR /build/cuda
# versions: https://developer.nvidia.com/cuda-toolkit-archive
ENV CUDA_VERSION="12.0.0"
ENV CUDA_BUILD="525.60.13"
# hadolint ignore=SC3010
RUN <<_INSTALL_CUDA
#!/bin/bash
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
COPY .. .

# setup npm dependencies
RUN npm install

# setup build directory
WORKDIR /build/sunshine/build

# cmake and cpack
RUN <<_MAKE
#!/bin/bash
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
cpack -G RPM
_MAKE

FROM scratch AS artifacts
ARG BASE
ARG TAG
ARG TARGETARCH
COPY --from=sunshine-build /build/sunshine/build/cpack_artifacts/Sunshine.rpm /sunshine-${BASE}-${TAG}-${TARGETARCH}.rpm

FROM sunshine-base as sunshine

# copy deb from builder
COPY --from=artifacts /sunshine*.rpm /sunshine.rpm

# install sunshine
RUN <<_INSTALL_SUNSHINE
#!/bin/bash
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
groupadd -f -g "${PGID}" "${UNAME}"
useradd -lm -d ${HOME} -s /bin/bash -g "${PGID}" -G input -u "${PUID}" "${UNAME}"
mkdir -p ${HOME}/.config/sunshine
ln -s ${HOME}/.config/sunshine /config
chown -R ${UNAME} ${HOME}
_SETUP_USER

USER ${UNAME}
WORKDIR ${HOME}

# entrypoint
ENTRYPOINT ["/usr/bin/sunshine"]
