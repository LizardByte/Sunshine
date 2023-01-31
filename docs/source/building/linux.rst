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
          libboost-program-options-dev \
          libboost-thread-dev \
          libcap-dev \  # KMS
          libcurl4-openssl-dev \
          libdrm-dev \  # KMS
          libevdev-dev \
          libmfx-dev \  # x86_64 only
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

Fedora 36, 37
^^^^^^^^^^^^^
End of Life: TBD

Install Requirements
   .. code-block:: bash

      sudo dnf update && \
      sudo dnf group install "Development Tools" && \
      sudo dnf install \
          boost-devel \
          cmake \
          gcc \
          gcc-c++ \
          intel-mediasdk-devel \ # x86_64 only
          libcap-devel \
          libcurl-devel \
          libdrm-devel \
          libevdev-devel \
          libva-devel \
          libvdpau-devel \
          libX11-devel \  # X11
          libxcb-devel \  # X11
          libXcursor-devel \  # X11
          libXfixes-devel \  # X11
          libXi-devel \  # X11
          libXinerama-devel \  # X11
          libXrandr-devel \  # X11
          libXtst-devel \  # X11
          mesa-libGL-devel \
          npm \
          numactl-devel \
          openssl-devel \
          opus-devel \
          pulseaudio-libs-devel \
          rpm-build \  # if you want to build an RPM binary package
          wget \  # necessary for cuda install with `run` file
          which   # necessary for cuda install with `run` file

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
          libboost-program-options-dev \
          libcap-dev \  # KMS
          libdrm-dev \  # KMS
          libevdev-dev \
          libmfx-dev \  # x86_64 only
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
          wget  # necessary for cuda install with `run` file

Update gcc alias
   .. code-block:: bash

      update-alternatives --install \
        /usr/bin/gcc gcc /usr/bin/gcc-10 100 \
        --slave /usr/bin/g++ g++ /usr/bin/g++-10 \
        --slave /usr/bin/gcov gcov /usr/bin/gcov-10 \
        --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-10 \
        --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-10

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
          libboost-program-options-dev \
          libcap-dev \  # KMS
          libdrm-dev \  # KMS
          libevdev-dev \
          libmfx-dev \  # x86_64 only
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
          nvidia-cuda-dev \  # CUDA, NvFBC
          nvidia-cuda-toolkit  # CUDA, NvFBC

CUDA
----
If the version of CUDA available from your distro is not adequate, manually install CUDA.

.. Tip:: The version of CUDA you use will determine compatibility with various GPU generations.
   See `CUDA compatibility <https://docs.nvidia.com/deploy/cuda-compatibility/index.html>`_ for more info.

   Select the appropriate run file based on your desired CUDA version and architecture according to
   `CUDA Toolkit Archive <https://developer.nvidia.com/cuda-toolkit-archive>`_.

.. code-block:: bash

   wget https://developer.download.nvidia.com/compute/cuda/11.4.2/local_installers/cuda_11.4.2_470.57.02_linux.run \
     --progress=bar:force:noscroll -q --show-progress -O ./cuda.run
   chmod a+x ./cuda.run
   ./cuda.run --silent --toolkit --toolkitpath=/usr --no-opengl-libs --no-man-page --no-drm
   rm ./cuda.run

npm dependencies
----------------
Install npm dependencies.
   .. code-block:: bash

      npm install

Build
-----
.. Attention:: Ensure you are in the build directory created during the clone step earlier before continuing.

.. code-block:: bash

   cmake ..
   make -j ${nproc}

   cpack -G DEB  # optionally, create a deb package
   cpack -G RPM  # optionally, create a rpm package
