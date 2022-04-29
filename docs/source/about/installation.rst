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

AppImage
^^^^^^^^
.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/pkg:appimage?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

#. Download and extract `sunshine-appimage.zip`

Debian Packages
^^^^^^^^^^^^^^^
.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/os:linux:debian?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

#. Download the corresponding `.deb` file, e.g. ``sunshine-ubuntu_20_04.deb``
#. ``sudo apt install -f <downloaded deb file>``, e.g. ``sudo apt install -f ./sunshine-ubuntu_20_04.deb``

Red Hat Packages
^^^^^^^^^^^^^^^^
.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/os:linux:fedora?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

#. Download the corresponding `.rpm` file, e.g. ``sunshine-fedora_35.rpm``
#. ``sudo dnf install <downloaded rpm file>``, e.g. ``sudo dnf install ./sunshine-fedora_35.rpm``

.. Hint:: If this is the first time installing.

      .. code-block:: bash

         sudo usermod -a -G input $USER
         sudo reboot now

.. Tip:: Optionally, run Sunshine in the background.

      .. code-block:: bash

         systemctl --user start sunshine

MacOS
-----
.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/os:macos?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

#. Install `MacPorts <https://www.macports.org>`_
#. Download the `Portfile <https://github.com/SunshineStream/Sunshine/blob/master/Portfile>`_ from this repository to
   ``/tmp``
#. In a terminal run ``cd /tmp && sudo port install``
#. The first time you start Sunshine, you will be asked to grant access to screen recording and your microphone.

Windows
-------
.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/os:windows:10?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

.. image:: https://img.shields.io/github/issues/sunshinestream/sunshine/os:windows:11?logo=github&style=for-the-badge
   :alt: GitHub issues by-label

#. Download and extract ``sunshine-windows.zip``

.. _latest release: https://github.com/SunshineStream/Sunshine/releases/latest
.. _Dockerhub.io: https://hub.docker.com/repository/docker/sunshinestream/sunshine
.. _ghcr.io: https://github.com/orgs/SunshineStream/packages?repo_name=sunshine
