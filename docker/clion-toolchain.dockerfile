# syntax=docker/dockerfile:1
# artifacts: false
# platforms: linux/amd64
# platforms_pr: linux/amd64
# no-cache-filters: toolchain-base,toolchain
ARG BASE=debian
ARG TAG=trixie-slim
FROM ${BASE}:${TAG} AS toolchain-base

ENV DEBIAN_FRONTEND=noninteractive

FROM toolchain-base AS toolchain

ARG TARGETPLATFORM
RUN echo "target_platform: ${TARGETPLATFORM}"

ENV DISPLAY=:0

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# install dependencies
RUN <<_DEPS
#!/bin/bash
set -e
apt-get update -y
apt-get install -y --no-install-recommends \
  build-essential \
  cmake=3.31.* \
  ca-certificates \
  doxygen \
  gcc=4:14.2.* \
  g++=4:14.2.* \
  gdb \
  git \
  graphviz \
  libayatana-appindicator3-dev \
  libcap-dev \
  libcurl4-openssl-dev \
  libdrm-dev \
  libevdev-dev \
  libgbm-dev \
  libminiupnpc-dev \
  libnotify-dev \
  libnuma-dev \
  libopus-dev \
  libpulse-dev \
  libssl-dev \
  libva-dev \
  libwayland-dev \
  libx11-dev \
  libxcb-shm0-dev \
  libxcb-xfixes0-dev \
  libxcb1-dev \
  libxfixes-dev \
  libxrandr-dev \
  libxtst-dev \
  npm \
  udev \
  wget \
  x11-xserver-utils \
  xvfb
apt-get clean
rm -rf /var/lib/apt/lists/*
_DEPS

# install cuda
WORKDIR /build/cuda
# versions: https://developer.nvidia.com/cuda-toolkit-archive
ENV CUDA_VERSION="12.9.1"
ENV CUDA_BUILD="575.57.08"
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
tmpfile="/tmp/cuda.run"
wget "$url" --progress=bar:force:noscroll --show-progress -O "$tmpfile"
chmod a+x "${tmpfile}"
"${tmpfile}" --silent --toolkit --toolkitpath=/usr/local --no-opengl-libs --no-man-page --no-drm
rm -f "${tmpfile}"
_INSTALL_CUDA

WORKDIR /toolchain
# Create a shell script that starts Xvfb and then runs a shell
RUN <<_ENTRYPOINT
#!/bin/bash
set -e
cat <<EOF > entrypoint.sh
#!/bin/bash
Xvfb ${DISPLAY} -screen 0 1024x768x24 &
if [ "\$#" -eq 0 ]; then
  exec "/bin/bash"
else
  exec "\$@"
fi
EOF

# Make the script executable
chmod +x entrypoint.sh

# Note about CLion
echo "ATTENTION: CLion will override the entrypoint, you can disable this in the toolchain settings"
_ENTRYPOINT

# Use the shell script as the entrypoint
ENTRYPOINT ["/toolchain/entrypoint.sh"]
