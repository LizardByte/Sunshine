Installation
============
The recommended method for running Sunshine is to use the `binaries`_ bundled with the `latest release`_.

.. Attention:: Additional setup is required after installation. See
   :ref:`Setup <about/usage:setup>`.

Binaries
--------
Binaries of Sunshine are created for each release. They are available for Linux, macOS, and Windows.
Binaries can be found in the `latest release`_.

.. Tip:: Some third party packages also exist. See
   :ref:`Third Party Packages <about/third_party_packages:third party packages>`.

Docker
------
Docker images are available on `Dockerhub.io`_ and `ghcr.io`_.

See :ref:`Docker <about/docker:docker>` for additional information.

Linux
-----
Follow the instructions for your preferred package type below.

**CUDA Compatibility**

CUDA is used for NVFBC capture.

.. Tip:: See `CUDA GPUS <https://developer.nvidia.com/cuda-gpus>`_ to cross reference Compute Capability to your GPU.

.. table::
   :widths: auto

   ===========================================  ==============   ==============    ================================
   Package                                      CUDA Version     Min Driver        CUDA Compute Capabilities
   ===========================================  ==============   ==============    ================================
   https://aur.archlinux.org/packages/sunshine  User dependent   User dependent    User dependent
   sunshine.AppImage                            11.8.0           450.80.02         50;52;60;61;62;70;75;80;86;90;35
   sunshine_{arch}.flatpak                      11.8.0           450.80.02         50;52;60;61;62;70;75;80;86;90;35
   sunshine-debian-bullseye-{arch}.deb          11.8.0           450.80.02         50;52;60;61;62;70;75;80;86;90;35
   sunshine-fedora-36-{arch}.rpm                12.0.0           525.60.13         50;52;60;61;62;70;75;80;86;90
   sunshine-fedora-37-{arch}.rpm                12.0.0           525.60.13         50;52;60;61;62;70;75;80;86;90
   sunshine-ubuntu-20.04-{arch}.deb             11.8.0           450.80.02         50;52;60;61;62;70;75;80;86;90;35
   sunshine-ubuntu-22.04-{arch}.deb             11.8.0           450.80.02         50;52;60;61;62;70;75;80;86;90;35
   ===========================================  ==============   ==============    ================================

AppImage
^^^^^^^^
According to AppImageLint the supported distro matrix of the AppImage is below.

- [✖] Debian oldstable (buster)
- [✔] Debian stable (bullseye)
- [✔] Debian testing (bookworm)
- [✔] Debian unstable (sid)
- [✔] Ubuntu kinetic
- [✔] Ubuntu jammy
- [✔] Ubuntu focal
- [✖] Ubuntu bionic
- [✖] Ubuntu xenial
- [✖] Ubuntu trusty
- [✖] CentOS 7

#. Download ``sunshine.AppImage`` to your home directory.
#. Open terminal and run the following code.

   .. code-block:: bash

      ./sunshine.AppImage --install

Start:
   .. code-block:: bash

      ./sunshine.AppImage --install && ./sunshine.AppImage

Uninstall:
   .. code-block:: bash

      ./sunshine.AppImage --remove

AUR Package
^^^^^^^^^^^
#. Open terminal and run the following code.

   .. code-block:: bash

      git clone https://aur.archlinux.org/sunshine.git
      cd sunshine
      makepkg -fi

Uninstall:
   .. code-block:: bash

      pacman -R sunshine

Debian Package
^^^^^^^^^^^^^^
#. Download ``sunshine-{ubuntu-version}.deb`` and run the following code.

   .. code-block:: bash

      sudo apt install -f ./sunshine-{ubuntu-version}.deb

.. Note:: The ``{ubuntu-version}`` is the version of ubuntu we built the package on. If you are not using Ubuntu and
   have an issue with one package, you can try another.

.. Tip:: You can double click the deb file to see details about the package and begin installation.

Uninstall:
   .. code-block:: bash

      sudo apt remove sunshine

Flatpak Package
^^^^^^^^^^^^^^^
#. Install `Flatpak <https://flatpak.org/setup/>`_ as required.
#. Download ``sunshine_{arch}.flatpak`` and run the following code.

   .. Note:: Be sure to replace ``{arch}`` with the architecture for your operating system.

   System level (recommended)
      .. code-block:: bash

         flatpak install --system ./sunshine_{arch}.flatpak

   User level
      .. code-block:: bash

         flatpak install --user ./sunshine_{arch}.flatpak

   Additional installation (required)
      .. code-block:: bash

         flatpak run --command=additional-install.sh dev.lizardbyte.sunshine

Start:
   X11 and NVFBC capture (X11 Only)
      .. code-block:: bash

         flatpak run dev.lizardbyte.sunshine

   KMS capture (Wayland & X11)
      .. code-block:: bash

         sudo -i PULSE_SERVER=unix:$(pactl info | awk '/Server String/{print$3}') flatpak run dev.lizardbyte.sunshine

Uninstall:
   .. code-block:: bash

      flatpak run --command=remove-additional-install.sh dev.lizardbyte.sunshine
      flatpak uninstall --delete-data dev.lizardbyte.sunshine

RPM Package
^^^^^^^^^^^
#. Add `rpmfusion` repositories by running the following code.

   .. code-block:: bash

      sudo dnf install https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
      https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm

#. Download ``sunshine.rpm`` and run the following code.

   .. code-block:: bash

      sudo dnf install ./sunshine.rpm

.. Tip:: You can double click the rpm file to see details about the package and begin installation.

Uninstall:
   .. code-block:: bash

      sudo dnf remove sunshine

macOS
-----
Sunshine on macOS is experimental. Gamepads do not work. Other features may not work as expected.

pkg
^^^
.. Warning:: The `pkg` does not include runtime dependencies.

#. Download the ``sunshine.pkg`` file and install it as normal.

Uninstall:
   .. code-block:: bash

      cd /etc/sunshine/assets
      uninstall_pkg.sh

Portfile
^^^^^^^^
#. Install `MacPorts <https://www.macports.org>`_
#. Update the Macports sources.

   .. code-block:: bash

      sudo nano /opt/local/etc/macports/sources.conf

   Add this line, replacing your username, below the line that starts with ``rsync``.
      ``file:///Users/<username>/ports``

   ``Ctrl+x``, then ``Y`` to exit and save changes.

#. Download the ``Portfile`` to ``~/Downloads`` and run the following code.

   .. code-block:: bash

      mkdir -p ~/ports/multimedia/sunshine
      mv ~/Downloads/Portfile ~/ports/multimedia/sunshine/
      cd ~/ports
      portindex
      sudo port install sunshine

#. The first time you start Sunshine, you will be asked to grant access to screen recording and your microphone.

Uninstall:
   .. code-block:: bash

      sudo port uninstall sunshine

Windows
-------

Installer
^^^^^^^^^
#. Download and install ``sunshine-windows.exe``

.. Attention:: You should carefully select or unselect the options you want to install. Do not blindly install or enable
   features.

To uninstall, find Sunshine in the list `here <ms-settings:installed-apps>`_ and select "Uninstall" from the overflow
menu. Different versions of Windows may provide slightly different steps for uninstall.

Standalone
^^^^^^^^^^
#. Download and extract ``sunshine-windows.zip``

To uninstall, delete the extracted directory which contains the ``sunshine.exe`` file.

.. _latest release: https://github.com/LizardByte/Sunshine/releases/latest
.. _Dockerhub.io: https://hub.docker.com/repository/docker/lizardbyte/sunshine
.. _ghcr.io: https://github.com/orgs/LizardByte/packages?repo_name=sunshine
