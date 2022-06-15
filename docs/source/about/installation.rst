:github_url: https://github.com/SunshineStream/Sunshine/tree/nightly/docs/source/about/installation.rst

Installation
============
The recommended method for running Sunshine is to use the `binaries`_ bundled with the `latest release`_.

Binaries
--------
Binaries of Sunshine are created for each release. They are available for Linux, and Windows.
Binaries can be found in the `latest release`_.

.. Todo:: Create binary package(s) for MacOS. See `here <https://github.com/SunshineStream/Sunshine/issues/61>`_.

.. Tip:: Some third party packages also exist. See
   :ref:`Third Party Packages <about/third_party_packages:third party packages>`.

Docker
------
.. Todo:: Docker images of Sunshine are planned to be included in the future.
   They will be available on `Dockerhub.io`_ and `ghcr.io`_.

Linux
-----
Follow the instructions for your preferred package type below.

AppImage
^^^^^^^^
.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/pkg:appimage?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

The current known compatibility of the AppImage is shown below.

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

#. Download and extract ``sunshine-appimage.zip`` to your home directory.

Debian Packages
^^^^^^^^^^^^^^^
.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/os:linux:debian?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

#. Download ``sunshine.deb`` and run the following code.

   .. code-block:: bash

      sudo apt install -f ./sunshine.deb

.. Tip:: You can double click the deb file to see details about the package and begin installation.

Red Hat Packages
^^^^^^^^^^^^^^^^
.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/os:linux:fedora?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

#. Add `rpmfusion` repositories by running the following code.

   .. code-block:: bash

      sudo dnf install https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
      https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm

#. Download ``sunshine.rpm`` and run the following code.

   .. code-block:: bash

      sudo dnf install ./sunshine.rpm

.. Tip:: You can double click the rpm file to see details about the package and begin installation.

MacOS
-----
.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/os:macos?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

Portfile
   #. Install `MacPorts <https://www.macports.org>`_
   #. Update the Macports sources.

      .. code-block:: bash

         sudo nano /opt/local/etc/macports/sources.conf

      Add this line, replacing your username, below the line that starts with ``rsync``.

         file://Users/<username>/ports

      ``Ctrl+x``, then ``Y`` to exit and save changes.

   #. Download the ``Portfile`` to ``~/Downloads`` and run the following code.

      .. code-block:: bash

         mkdir -p ~/ports/multimedia/sunshine
         mv ~/Downlaods/Portfile ~/ports/multimedia/sunshine
         cd ~/ports
         portindex
         sudo port install sunshine

   #. The first time you start Sunshine, you will be asked to grant access to screen recording and your microphone.

Windows
-------
.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/os:windows:10?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/os:windows:11?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

Installed option:
   #. Download and install ``sunshine-windows.exe``

Standalone option:
   #. Download and extract ``sunshine-windows.zip``

.. _latest release: https://github.com/SunshineStream/Sunshine/releases/latest
.. _Dockerhub.io: https://hub.docker.com/repository/docker/sunshinestream/sunshine
.. _ghcr.io: https://github.com/orgs/SunshineStream/packages?repo_name=sunshine
