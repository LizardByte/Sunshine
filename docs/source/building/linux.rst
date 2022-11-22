Linux
=====

Requirements
------------

Debian Bullseye
^^^^^^^^^^^^^^^
End of Life: TBD

Install Requirements
   .. code-block:: bash

      sudo apt update && sudo apt install \
          build-essential \
          cmake \
          libavdevice-dev \
          libboost-filesystem-dev \
          libboost-log-dev \
          libboost-thread-dev \
          libcap-dev \  # KMS
          libdrm-dev \  # KMS
          libevdev-dev \
          libpulse-dev \
          libopus-dev \
          libssl-dev \
          libwayland-dev \  # Wayland
          libx11-dev \  # X11
          libxcb-shm0-dev \  # X11
          libxcb-xfixes0-dev \  # X11
          libxcb1-dev \  # X11
          libxfixes-dev \  # X11
          libxrandr-dev \  # X11
          libxtst-dev \  # X11
          nodejs \
          npm \
          nvidia-cuda-dev \  # Cuda, NvFBC
          nvidia-cuda-toolkit \  # Cuda, NvFBC

Fedora 35
^^^^^^^^^
End of Life: TBD

Install Repositories
   .. code-block:: bash

      sudo dnf update && \
          sudo dnf group install "Development Tools" && \
          sudo dnf install https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm

Install Requirements
   .. code-block:: bash

      sudo dnf install \
          boost-devel \
          boost-static.x86_64 \
          cmake \
          gcc-c++ \
          libevdev-devel \
          libX11-devel \  # X11
          libxcb-devel \  # X11
          libXcursor-devel \  # X11
          libXfixes-devel \  # X11
          libXinerama-devel \  # X11
          libXi-devel \  # X11
          libXrandr-devel \  # X11
          libXtst-devel \  # X11
          mesa-libGL-devel \
          nodejs \
          npm \
          openssl-devel \
          opus-devel \
          pulseaudio-libs-devel \
          rpm-build \  # if you want to build an RPM binary package

Ubuntu 18.04
^^^^^^^^^^^^
End of Life: April 2028

Install Repositories
   .. code-block:: bash

      sudo apt update && sudo apt install \
          software-properties-common \
      && add-apt-repository ppa:savoury1/graphics && \
      add-apt-repository ppa:savoury1/multimedia && \
      add-apt-repository ppa:savoury1/boost-defaults-1.71 && \
      add-apt-repository ppa:ubuntu-toolchain-r/test && \

Install Requirements
   .. code-block:: bash

      sudo apt install \
          build-essential \
          cmake \
          gcc-10 \
          g++-10 \
          libavdevice-dev \
          libboost-filesystem1.71-dev \
          libboost-log1.71-dev \
          libboost-regex1.71-dev \
          libboost-thread1.71-dev \
          libcap-dev \  # KMS
          libdrm-dev \  # KMS
          libevdev-dev \
          libpulse-dev \
          libopus-dev \
          libssl-dev \
          libwayland-dev \  # Wayland
          libx11-dev \  # X11
          libxcb-shm0-dev \  # X11
          libxcb-xfixes0-dev \  # X11
          libxcb1-dev \  # X11
          libxfixes-dev \  # X11
          libxrandr-dev \  # X11
          libxtst-dev \  # X11
          nodejs \
          npm \
          wget \

Update gcc alias
   .. code-block:: bash

      update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100 --slave /usr/bin/g++ g++ /usr/bin/g++-10

Install CuDA
   .. code-block:: bash

      wget https://developer.download.nvidia.com/compute/cuda/11.4.2/local_installers/cuda_11.4.2_470.57.02_linux.run --progress=bar:force:noscroll -q --show-progress -O ./cuda.run && chmod a+x ./cuda.run
      ./cuda.run --silent --toolkit --toolkitpath=/usr --no-opengl-libs --no-man-page --no-drm && rm ./cuda.run

Install CMake
   .. code-block:: bash

      wget https://cmake.org/files/v3.22/cmake-3.22.2-linux-x86_64.sh
      mkdir /opt/cmake
      sh /cmake-3.22.2-linux-x86_64.sh --prefix=/opt/cmake --skip-license
      ln -s /opt/cmake/bin/cmake /usr/local/bin/cmake
      cmake --version

Ubuntu 20.04
^^^^^^^^^^^^
End of Life: April 2030

Install Requirements
   .. code-block:: bash

      sudo apt update && sudo apt install \
          build-essential \
          cmake \
          g++-10 \
          libavdevice-dev \
          libboost-filesystem-dev \
          libboost-log-dev \
          libboost-thread-dev \
          libcap-dev \  # KMS
          libdrm-dev \  # KMS
          libevdev-dev \
          libpulse-dev \
          libopus-dev \
          libssl-dev \
          libwayland-dev \  # Wayland
          libx11-dev \  # X11
          libxcb-shm0-dev \  # X11
          libxcb-xfixes0-dev \  # X11
          libxcb1-dev \  # X11
          libxfixes-dev \  # X11
          libxrandr-dev \  # X11
          libxtst-dev \  # X11
          nodejs \
          npm \
          wget \

Update gcc alias
   .. code-block:: bash

      update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100 --slave /usr/bin/g++ g++ /usr/bin/g++-10

Install CuDA
   .. code-block:: bash

      wget https://developer.download.nvidia.com/compute/cuda/11.4.2/local_installers/cuda_11.4.2_470.57.02_linux.run --progress=bar:force:noscroll -q --show-progress -O ./cuda.run && chmod a+x ./cuda.run
      ./cuda.run --silent --toolkit --toolkitpath=/usr --no-opengl-libs --no-man-page --no-drm && rm ./cuda.run

Ubuntu 22.04
^^^^^^^^^^^^
End of Life: April 2027

Install Requirements
   .. code-block:: bash

      sudo apt update && sudo apt install \
          build-essential \
          cmake \
          libavdevice-dev \
          libboost-filesystem-dev \
          libboost-log-dev \
          libboost-thread-dev \
          libcap-dev \  # KMS
          libdrm-dev \  # KMS
          libevdev-dev \
          libpulse-dev \
          libopus-dev \
          libssl-dev \
          libwayland-dev \  # Wayland
          libx11-dev \  # X11
          libxcb-shm0-dev \  # X11
          libxcb-xfixes0-dev \  # X11
          libxcb1-dev \  # X11
          libxfixes-dev \  # X11
          libxrandr-dev \  # X11
          libxtst-dev \  # X11
          nodejs \
          npm \
          nvidia-cuda-dev \  # Cuda, NvFBC
          nvidia-cuda-toolkit \  # Cuda, NvFBC

npm dependencies
----------------
Install npm dependencies.
   .. code-block:: bash

      pushd ./src_assets/common/assets/web
      npm install
      popd

Build
-----
.. Attention:: Ensure you are in the build directory created during the clone step earlier before continuing.

Debian based OSes
   .. code-block:: bash

      cmake -DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10 ..

Red Hat based OSes
   .. code-block:: bash

      cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ..

Finally
   .. code-block:: bash

      make -j ${nproc}
      cpack -G DEB  # optionally, create a deb package
      cpack -G RPM  # optionally, create a rpm package
