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
          libnuma-dev \
          libopus-dev \
          libpulse-dev \
          libssl-dev \
          libva-dev \
          libvdpau-dev \
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
          nvidia-cuda-toolkit  # Cuda, NvFBC

Fedora 36
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
          libva-devel \
          libvdpau-devel \
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
          numactl-devel \
          openssl-devel \
          opus-devel \
          pulseaudio-libs-devel \
          rpm-build  # if you want to build an RPM binary package

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
          libnuma-dev \
          libopus-dev \
          libpulse-dev \
          libssl-dev \
          libva-dev \
          libvdpau-dev \
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
          wget

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
          libnuma-dev \
          libopus-dev \
          libpulse-dev \
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
          nvidia-cuda-toolkit  # Cuda, NvFBC

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

.. code-block:: bash

   cmake ..
   make -j ${nproc}

   cpack -G DEB  # optionally, create a deb package
   cpack -G RPM  # optionally, create a rpm package
