Overview
========
LizardByte has the full documentation hosted on `Read the Docs <https://sunshinestream.readthedocs.io/>`__.

About
-----
Sunshine is a self-hosted game stream host for Moonlight.
Offering low latency, cloud gaming server capabilities with support for AMD, Intel, and Nvidia GPUs for hardware
encoding. Software encoding is also available. You can connect to Sunshine from any Moonlight client on a variety of
devices. A web UI is provided to allow configuration, and client pairing, from your favorite web browser. Pair from
the local server or any mobile device.

System Requirements
-------------------

.. warning:: This table is a work in progress. Do not purchase hardware based on this.

**Minimum Requirements**

.. csv-table::
   :widths: 15, 60

   "GPU", "AMD: VCE 1.0 or higher, see: `obs-amd hardware support <https://github.com/obsproject/obs-amd-encoder/wiki/Hardware-Support>`_"
   "", "Intel: VAAPI-compatible, see: `VAAPI hardware support <https://www.intel.com/content/www/us/en/developer/articles/technical/linuxmedia-vaapi.html>`_"
   "", "Nvidia: NVENC enabled cards, see: `nvenc support matrix <https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new>`_"
   "CPU", "AMD: Ryzen 3 or higher"
   "", "Intel: Core i3 or higher"
   "RAM", "4GB or more"
   "OS", "Windows: 10+ (Windows Server does not support virtual gamepads)"
   "", "macOS: 12+"
   "", "Linux/Debian: 11 (bullseye)"
   "", "Linux/Fedora: 39+"
   "", "Linux/Ubuntu: 22.04+ (jammy)"
   "Network", "Host: 5GHz, 802.11ac"
   "", "Client: 5GHz, 802.11ac"

**4k Suggestions**

.. csv-table::
   :widths: 15, 60

   "GPU", "AMD: Video Coding Engine 3.1 or higher"
   "", "Intel: HD Graphics 510 or higher"
   "", "Nvidia: GeForce GTX 1080 or higher"
   "CPU", "AMD: Ryzen 5 or higher"
   "", "Intel: Core i5 or higher"
   "Network", "Host: CAT5e ethernet or better"
   "", "Client: CAT5e ethernet or better"

**HDR Suggestions**

.. csv-table::
   :widths: 15, 60

   "GPU", "AMD: Video Coding Engine 3.4 or higher"
   "", "Intel: UHD Graphics 730 or higher"
   "", "Nvidia: Pascal-based GPU (GTX 10-series) or higher"
   "CPU", "AMD: todo"
   "", "Intel: todo"
   "Network", "Host: CAT5e ethernet or better"
   "", "Client: CAT5e ethernet or better"

Integrations
------------

.. image:: https://img.shields.io/github/actions/workflow/status/lizardbyte/sunshine/CI.yml.svg?branch=master&label=CI%20build&logo=github&style=for-the-badge
   :alt: GitHub Workflow Status (CI)
   :target: https://github.com/LizardByte/Sunshine/actions/workflows/CI.yml?query=branch%3Amaster

.. image:: https://img.shields.io/github/actions/workflow/status/lizardbyte/sunshine/localize.yml.svg?branch=master&label=localize%20build&logo=github&style=for-the-badge
   :alt: GitHub Workflow Status (localize)
   :target: https://github.com/LizardByte/Sunshine/actions/workflows/localize.yml?query=branch%3Amaster

.. image:: https://img.shields.io/readthedocs/sunshinestream.svg?label=Docs&style=for-the-badge&logo=readthedocs
   :alt: Read the Docs
   :target: http://sunshinestream.readthedocs.io/

.. image:: https://img.shields.io/codecov/c/gh/LizardByte/Sunshine?token=SMGXQ5NVMJ&style=for-the-badge&logo=codecov&label=codecov
   :alt: Codecov
   :target: https://codecov.io/gh/LizardByte/Sunshine

Support
-------

Our support methods are listed in our
`LizardByte Docs <https://lizardbyte.readthedocs.io/en/latest/about/support.html>`__.

Downloads
---------

.. image:: https://img.shields.io/github/downloads/lizardbyte/sunshine/total.svg?style=for-the-badge&logo=github
   :alt: GitHub Releases
   :target: https://github.com/LizardByte/Sunshine/releases/latest

.. image:: https://img.shields.io/docker/pulls/lizardbyte/sunshine.svg?style=for-the-badge&logo=docker
   :alt: Docker
   :target: https://hub.docker.com/r/lizardbyte/sunshine

.. image:: https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2Fipitio%2Fghcr-pulls%2Fmaster%2Findex.json&query=%24%5B%3F(%40.owner%3D%3D%22LizardByte%22%20%26%26%20%40.repo%3D%3D%22Sunshine%22%20%26%26%20%40.image%3D%3D%22sunshine%22)%5D.pulls&label=ghcr%20pulls&style=for-the-badge&logo=github
   :alt: GHCR
   :target: https://github.com/LizardByte/Sunshine/pkgs/container/sunshine

.. image:: https://img.shields.io/badge/dynamic/json.svg?color=orange&label=Winget&style=for-the-badge&prefix=v&query=$[-1:].name&url=https%3A%2F%2Fapi.github.com%2Frepos%2Fmicrosoft%2Fwinget-pkgs%2Fcontents%2Fmanifests%2Fl%2FLizardByte%2FSunshine&logo=microsoft
   :alt: Winget Version
   :target: https://github.com/microsoft/winget-pkgs/tree/master/manifests/l/LizardByte/Sunshine

Stats
------
.. image:: https://img.shields.io/github/stars/lizardbyte/sunshine.svg?logo=github&style=for-the-badge
   :alt: GitHub stars
   :target: https://github.com/LizardByte/Sunshine
