:github_url: https://github.com/SunshineStream/Sunshine/tree/nightly/docs/source/building/linux.rst

Linux
=====

Requirements
------------
.. Danger:: Installing these dependencies may break your distribution. It is recommended to build in a virtual machine
   or to use the `Dockerfile builds`_ located in the `./scripts` directory.

Debian Bullseye
^^^^^^^^^^^^^^^
End of Life: TBD

Install Requirements
   .. code-block:: bash

      sudo apt update && sudo apt install \
          build-essential \
          cmake \
          git \
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
          ffmpeg-devel \
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
      add-apt-repository ppa:savoury1/ffmpeg4 && \
      add-apt-repository ppa:savoury1/boost-defaults-1.71 && \
      add-apt-repository ppa:ubuntu-toolchain-r/test && \

Install Requirements
   .. code-block:: bash

      sudo apt install \
          build-essential \
          cmake \
          gcc-10 \
          git \
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
          git \
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
          wget \

Update gcc alias
   .. code-block:: bash

      update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100 --slave /usr/bin/g++ g++ /usr/bin/g++-10

Install CuDA
   .. code-block:: bash

      wget https://developer.download.nvidia.com/compute/cuda/11.4.2/local_installers/cuda_11.4.2_470.57.02_linux.run --progress=bar:force:noscroll -q --show-progress -O ./cuda.run && chmod a+x ./cuda.run
      ./cuda.run --silent --toolkit --toolkitpath=/usr --no-opengl-libs --no-man-page --no-drm && rm ./cuda.run

Ubuntu 21.10
^^^^^^^^^^^^
End of Life: July 2022

Install Requirements
   .. code-block:: bash

      sudo apt update && sudo apt install \
          build-essential \
          cmake \
          git \
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
          nvidia-cuda-dev \  # Cuda, NvFBC
          nvidia-cuda-toolkit \  # Cuda, NvFBC

Ubuntu 22.04
^^^^^^^^^^^^
End of Life: April 2027

.. Todo:: Create Ubuntu 22.04 Dockerfile and complete this documentation.

Build
-----
.. Attention:: Ensure you are in the build directory created during the clone step earlier before continuing.

Debian based OSes
   .. code-block:: bash

      cmake -DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10 ..

Red Hat based Oses
   .. code-block:: bash

      cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ..

Finally
   .. code-block:: bash

      make -j ${nproc}

Dockerfile Builds
-----------------
You may wish to simply build sunshine from source, without bloating your OS with development files.
There are scripts located in the ``./scripts`` directory that will create docker images that have the necessary
packages. As a result, removing the development files after you're done is a single command away.
These scripts use docker under the hood, as such, they can only be used to compile the Linux version

.. Todo:: Publish the Dockerfiles to Dockerhub and ghcr.

Requirements
   Install `Docker <https://docs.docker.com/engine/install/>`_

Instructions
   #. :ref:`Clone <building/build:clone>`. Sunshine.
   #. Select the desired Dockerfile from the ``./scripts`` directory.

      Available Files:
         .. code-block:: text

            Dockerfile-debian
            Dockerfile-fedora_33  # end of life
            Dockerfile-fedora_35
            Dockerfile-ubuntu_18_04
            Dockerfile-ubuntu_20_04
            Dockerfile-ubuntu_21_04  # end of life
            Dockerfile-ubuntu_21_10

   #. Execute

      .. code-block:: bash

         cd scripts  # move to the scripts directory
         ./build-container.sh -f Dockerfile-<name>  # create the container (replace the "<name>")
         ./build-sunshine.sh -p -s ..  # compile and build sunshine

   #. Updating

      .. code-block:: bash

         git pull  # pull the latest changes from github
         ./build-sunshine.sh -p -s ..  # compile and build sunshine

   #. Optionally, delete the container
      .. code-block:: bash

         ./build-container.sh -c delete

   #. Install the resulting package

      Debian
         .. code-block:: bash

            sudo apt install -f sunshine-build/sunshine.deb

      Red Hat
         .. code-block:: bash

            sudo dnf install sunshine-build/sunshine.rpm
