:github_url: https://github.com/LizardByte/Sunshine/tree/nightly/docs/source/about/installation.rst

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
First, follow the instructions for your preferred package type below.

Then start sunshine with the following command, unless a start command is listed in the specified package.

.. code-block:: bash

   sunshine

AppImage
^^^^^^^^
.. image:: https://img.shields.io/github/issues/lizardbyte/sunshine/pkg:appimage?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

According to AppImageLint the AppImage can run on the following distros.

   - [✖] Debian oldstable (buster)
   - [✔] Debian stable (bullseye)
   - [✔] Debian testing (bookworm)
   - [✔] Debian unstable (sid)
   - [✔] Ubuntu jammy
   - [✔] Ubuntu impish
   - [✔] Ubuntu focal
   - [✖] Ubuntu bionic
   - [✖] Ubuntu xenial
   - [✖] Ubuntu trusty
   - [✖] CentOS 7

#. Download ``sunshine-appimage.zip`` and extract the contents to your home directory.
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
.. image:: https://img.shields.io/github/issues/lizardbyte/sunshine/pkg:deb?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

#. Download ``sunshine.deb`` and run the following code.

   .. code-block:: bash

      sudo apt install -f ./sunshine.deb

.. Tip:: You can double click the deb file to see details about the package and begin installation.

Uninstall:

   .. code-block:: bash

      sudo apt remove sunshine

Flatpak Package
^^^^^^^^^^^^^^^
.. image:: https://img.shields.io/github/issues/lizardbyte/sunshine/pkg:flatpak?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

#. Install `Flatpak <https://flatpak.org/setup/>`_ as required.
#. Download ``sunshine.flatpak`` and run the following code.

   System level (recommended)
      .. code-block:: bash

         flatpak install --system sunshine.flatpak

   User level
      .. code-block:: bash

         flatpak install --user sunshine.flatpak

Start:

   X11 and NVFBC capture (X11 Only)
      .. code-block:: bash

         flatpak run dev.lizardbyte.sunshine

   KMS capture (Wayland & X11)
      .. code-block:: bash

         sudo -i PULSE_SERVER=unix:$(pactl info | awk '/Server String/{print$3}') flatpak run dev.lizardbyte.sunshine

Uninstall:

   .. code-block:: bash

      flatpak uninstall --delete-data sunshine.flatpak

RPM Package
^^^^^^^^^^^
.. image:: https://img.shields.io/github/issues/lizardbyte/sunshine/pkg:rpm?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

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
.. image:: https://img.shields.io/github/issues/lizardbyte/sunshine/os:macos?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

pkg
^^^
.. Warning:: The `pkg` does not include runtime dependencies and should be considered experimental.

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
.. image:: https://img.shields.io/github/issues/lizardbyte/sunshine/os:windows:10?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

.. image:: https://img.shields.io/github/issues/lizardbyte/sunshine/os:windows:11?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

Installer
^^^^^^^^^
#. Download and install ``sunshine-windows.exe``

To uninstall, find Sunshine in the list `here <ms-settings:installed-apps>`_ and select "Uninstall" from the overflow
menu. Different versions of Windows may provide slightly different steps for uninstall.

Standalone
^^^^^^^^^^
#. Download and extract ``sunshine-windows.zip``

To uninstall, delete the extracted directory which contains the ``sunshine.exe`` file.

.. _latest release: https://github.com/LizardByte/Sunshine/releases/latest
.. _Dockerhub.io: https://hub.docker.com/repository/docker/lizardbyte/sunshine
.. _ghcr.io: https://github.com/orgs/LizardByte/packages?repo_name=sunshine
