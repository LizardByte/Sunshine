# install dependencies for C++ analysis
set -e

CUDA_VERSION=11.8.0
CUDA_BUILD=520.61.05

# install wget and cuda first
sudo apt-get update -y
sudo apt-get install -y \
  wget

# Install CUDA
url_base="https://developer.download.nvidia.com/compute/cuda/${CUDA_VERSION}/local_installers"
url="${url_base}/cuda_${CUDA_VERSION}_${CUDA_BUILD}_linux.run"
sudo wget -q -O /root/cuda.run ${url}
sudo chmod a+x /root/cuda.run
sudo /root/cuda.run --silent --toolkit --toolkitpath=/usr/local/cuda --no-opengl-libs --no-man-page --no-drm
sudo rm /root/cuda.run

# Install dependencies
sudo apt-get install -y \
  build-essential \
  gcc-10 \
  g++-10 \
  libayatana-appindicator3-dev \
  libavdevice-dev \
  libcap-dev \
  libcurl4-openssl-dev \
  libdrm-dev \
  libevdev-dev \
  libminiupnpc-dev \
  libmfx-dev \
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
  libxtst-dev

# clean apt cache
sudo apt-get clean
sudo rm -rf /var/lib/apt/lists/*

# Update gcc alias
# https://stackoverflow.com/a/70653945/11214013
sudo update-alternatives --install \
  /usr/bin/gcc gcc /usr/bin/gcc-10 100 \
  --slave /usr/bin/g++ g++ /usr/bin/g++-10 \
  --slave /usr/bin/gcov gcov /usr/bin/gcov-10 \
  --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-10 \
  --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-10

# build
mkdir -p build
cd build || exit 1
cmake \
  -DBUILD_DOCS=OFF \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc \
  -G "Unix Makefiles" \
  ..
make -j"$(nproc)"

# Delete CUDA
sudo rm -rf /usr/local/cuda

# skip autobuild
echo "skip_autobuild=true" >> "$GITHUB_OUTPUT"
