********
Building
********

Linux
#####

If you do not wish to clutter your PC with development files, yet you want the very latest version.
You can use these `build scripts <https://github.com/salty2011/Sunshine/blob/nightly/scripts/README.md>`_  They make use of docker to handle building Sunshine automatically

Requirements
************
|

Ubuntu
^^^^^^
|
| Install the following
|

.. code-block:: sh
   :caption: Common

   sudo apt install \
            cmake \
            gcc-10 \
            g++-10 \
            libssl-dev \ 
            libavdevice-dev \
            libboost-thread-dev \
            libboost-filesystem-dev \
            libboost-log-dev \
            libpulse-dev \
            libopus-dev \
            libevdev-dev

|

.. code-block:: sh
   :caption: X11

   sudo apt install \
            libxtst-dev \
            libx11-dev \
            libxrandr-dev \
            libxfixes-dev \
            libxcb1-dev \
            libxcb-shm0-dev \
            libxcb-xfixes0-dev


|

.. code-block:: sh
   :caption: KMS

   sudo apt install libdrm-dev libcap-dev


|

.. code-block:: sh
   :caption: Wayland

   sudo apt install libwayland-dev

|

**Cuda + NvFBC**

|
| This requires proprietary software On Ubuntu 20.04, the cuda compiler will fail since it's version is too old, it's recommended you compile the sources with the `build scripts <https://github.com/salty2011/Sunshine/blob/SphinxDocs/scripts/README.md>`_ 
|

Fedora 35
^^^^^^^^^
|
| You will need some things in the RPMFusion repo, nost notably ffmpeg.
|

.. code-block:: sh

   sudo dnf install https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm

|

**Development tools and libraries**

.. code-block:: sh

   sudo dnf install \
         boost-devel \
         boost-static.x86_64 \
         cmake \
         ffmpeg-devel \
         gcc-c++ \
         libevdev-devel \
         libxcb-devel \
         libX11-devel \
         libXcursor-devel \
         libXfixes-devel \
         libXinerama-devel \
         libXi-devel \
         libXrandr-devel \
         libXtst-devel \
         mesa-libGL-devel \
         openssl-devel \
         opus-devel \
         pulseaudio-libs-devel

|
| If you need to build an RPM binary package: 

.. code-block:: sh

   sudo dnf install rpmbuild

|

.. warning:: You might require ffmpeg version >= 4.3 Check the troubleshooting section for more information.

