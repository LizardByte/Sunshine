# install dependencies for C++ analysis

sudo apt-get update -y
sudo apt-get install -y \
  build-essential \
  gcc-10 \
  g++-10 \
  libayatana-appindicator3-dev \
  libavdevice-dev \
  libboost-filesystem-dev \
  libboost-locale-dev \
  libboost-log-dev \
  libboost-program-options-dev \
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
  libxtst-dev \
  wget

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

# Install CUDA
sudo wget \
  https://developer.download.nvidia.com/compute/cuda/11.8.0/local_installers/cuda_11.8.0_520.61.05_linux.run \
  --progress=bar:force:noscroll -q --show-progress -O /root/cuda.run
sudo chmod a+x /root/cuda.run
sudo /root/cuda.run --silent --toolkit --toolkitpath=/usr --no-opengl-libs --no-man-page --no-drm
sudo rm /root/cuda.run
