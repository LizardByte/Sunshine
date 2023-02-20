Overview
========
LizardByte has the full documentation hosted on `Read the Docs <https://sunshinestream.readthedocs.io/>`_.

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

+------------+------------------------------------------------------------+
| GPU        | AMD: VCE 1.0 or higher, see `obs-amd hardware support`_    |
|            +------------------------------------------------------------+
|            | Intel: VAAPI-compatible, see: `VAAPI hardware support`_    |
|            +------------------------------------------------------------+
|            | Nvidia: NVENC enabled cards, see `nvenc support matrix`_   |
+------------+------------------------------------------------------------+
| CPU        | AMD: Ryzen 3 or higher                                     |
|            +------------------------------------------------------------+
|            | Intel: Core i3 or higher                                   |
+------------+------------------------------------------------------------+
| RAM        | 4GB or more                                                |
+------------+------------------------------------------------------------+
| OS         | Windows: 10+ (Windows Server not supported)                |
|            +------------------------------------------------------------+
|            | macOS: 11.7+                                               |
|            +------------------------------------------------------------+
|            | Linux/Debian: 11 (bullseye)                                |
|            +------------------------------------------------------------+
|            | Linux/Fedora: 36+                                          |
|            +------------------------------------------------------------+
|            | Linux/Ubuntu: 20.04+ (focal)                               |
+------------+------------------------------------------------------------+
| Network    | Host: 5GHz, 802.11ac                                       |
|            +------------------------------------------------------------+
|            | Client: 5GHz, 802.11ac                                     |
+------------+------------------------------------------------------------+

**4k Suggestions**

+------------+------------------------------------------------------------+
| GPU        | AMD: Video Coding Engine 3.1 or higher                     |
|            +------------------------------------------------------------+
|            | Intel: HD Graphics 510 or higher                           |
|            +------------------------------------------------------------+
|            | Nvidia: GeForce GTX 1080 or higher                         |
+------------+------------------------------------------------------------+
| CPU        | AMD: Ryzen 5 or higher                                     |
|            +------------------------------------------------------------+
|            | Intel: Core i5 or higher                                   |
+------------+------------------------------------------------------------+
| Network    | Host: CAT5e ethernet or better                             |
|            +------------------------------------------------------------+
|            | Client: CAT5e ethernet or better                           |
+------------+------------------------------------------------------------+

**HDR Suggestions**

+------------+------------------------------------------------------------+
| GPU        | AMD: Video Coding Engine 3.4 or higher                     |
|            +------------------------------------------------------------+
|            | Intel: UHD Graphics 730 or higher                          |
|            +------------------------------------------------------------+
|            | Nvidia: Pascal-based GPU (GTX 10-series) or higher         |
+------------+------------------------------------------------------------+
| CPU        | AMD: todo                                                  |
|            +------------------------------------------------------------+
|            | Intel: todo                                                |
+------------+------------------------------------------------------------+
| Network    | Host: CAT5e ethernet or better                             |
|            +------------------------------------------------------------+
|            | Client: CAT5e ethernet or better                           |
+------------+------------------------------------------------------------+

Integrations
------------

.. image:: https://img.shields.io/github/actions/workflow/status/lizardbyte/sunshine/CI.yml.svg?branch=master&label=CI%20build&logo=github&style=for-the-badge
   :alt: GitHub Workflow Status (CI)
   :target: https://github.com/LizardByte/Sunshine/actions/workflows/CI.yml?query=branch%3Amaster

.. image:: https://img.shields.io/github/actions/workflow/status/lizardbyte/sunshine/localize.yml.svg?branch=nightly&label=localize%20build&logo=github&style=for-the-badge
   :alt: GitHub Workflow Status (localize)
   :target: https://github.com/LizardByte/Sunshine/actions/workflows/localize.yml?query=branch%3Anightly

.. image:: https://img.shields.io/readthedocs/sunshinestream?label=Docs&style=for-the-badge&logo=readthedocs
   :alt: Read the Docs
   :target: http://sunshinestream.readthedocs.io/

.. image:: https://img.shields.io/badge/dynamic/json?color=blue&label=localized&style=for-the-badge&query=%24.progress..data.translationProgress&url=https%3A%2F%2Fbadges.awesome-crowdin.com%2Fstats-15178612-503956.json&logo=crowdin
   :alt: CrowdIn
   :target: https://crowdin.com/project/sunshinestream

Support
-------

Our support methods are listed in our
`LizardByte Docs <https://lizardbyte.readthedocs.io/en/latest/about/support.html>`_.

Downloads
---------

.. image:: https://img.shields.io/github/downloads/lizardbyte/sunshine/total?style=for-the-badge&logo=github
   :alt: GitHub Releases
   :target: https://github.com/LizardByte/Sunshine/releases/latest

.. image:: https://img.shields.io/docker/pulls/lizardbyte/sunshine?style=for-the-badge&logo=docker
   :alt: Docker
   :target: https://hub.docker.com/r/lizardbyte/sunshine

Stats
------
.. image:: https://img.shields.io/github/stars/lizardbyte/sunshine?logo=github&style=for-the-badge
   :alt: GitHub stars
   :target: https://github.com/LizardByte/Sunshine

.. _nvenc support matrix: https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new
.. _obs-amd hardware support: https://github.com/obsproject/obs-amd-encoder/wiki/Hardware-Support
.. _VAAPI hardware support: https://www.intel.com/content/www/us/en/developer/articles/technical/linuxmedia-vaapi.html
