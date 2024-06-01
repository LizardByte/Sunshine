# syntax=docker/dockerfile:1.4
# artifacts: false
# platforms: linux/amd64
# platforms_pr: linux/amd64
# no-cache-filters: toolchain-base,toolchain
ARG BASE=ubuntu
ARG TAG=22.04
FROM ${BASE}:${TAG} AS toolchain-base

ENV DEBIAN_FRONTEND=noninteractive

FROM toolchain-base as toolchain

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
  cmake=3.22.* \
  ca-certificates \
  doxygen \
  gcc=4:11.2.* \
  g++=4:11.2.* \
  gdb \
  git \
  graphviz \
  libayatana-appindicator3-dev \
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
  python3.10 \
  python3.10-venv \
  udev \
  wget \
  x11-xserver-utils \
  xvfb
apt-get clean
rm -rf /var/lib/apt/lists/*
_DEPS

#Install Node
# hadolint ignore=SC1091
RUN <<_INSTALL_NODE
#!/bin/bash
set -e
node_version="20.9.0"
wget -qO- https://raw.githubusercontent.com/nvm-sh/nvm/master/install.sh | bash
source "$HOME/.nvm/nvm.sh"
nvm install "$node_version"
nvm use "$node_version"
nvm alias default "$node_version"
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
if [[ "${TARGETPLATFORM}" == 'linux/arm64' ]]; then
  cuda_suffix="_sbsa"
fi
url="${cuda_prefix}${CUDA_VERSION}/local_installers/cuda_${CUDA_VERSION}_${CUDA_BUILD}_linux${cuda_suffix}.run"
echo "cuda url: ${url}"
wget "$url" --progress=bar:force:noscroll -q --show-progress -O ./cuda.run
chmod a+x ./cuda.run
./cuda.run --silent --toolkit --toolkitpath=/usr/local --no-opengl-libs --no-man-page --no-drm
rm ./cuda.run
_INSTALL_CUDA

WORKDIR /
# Write a shell script that starts Xvfb and then runs a shell
RUN <<_ENTRYPOINT
#!/bin/bash
set -e
cat <<EOF > /entrypoint.sh
#!/bin/bash
Xvfb ${DISPLAY} -screen 0 1024x768x24 &
if [ "\$#" -eq 0 ]; then
  exec "/bin/bash"
else
  exec "\$@"
fi
EOF
_ENTRYPOINT

# Make the script executable
RUN chmod +x /entrypoint.sh

# Note about CLion
RUN echo "ATTENTION: CLion will override the entrypoint, you can disable this in the toolchain settings"

# Use the shell script as the entrypoint
ENTRYPOINT ["/entrypoint.sh"]
