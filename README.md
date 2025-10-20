<div align="center">
  <img src="sunshine.png" />
  <h1 align="center">Sunshine</h1>
  <h4 align="center">Self-hosted game stream host for Moonlight.</h4>
</div>

<div align="center">
  <a href="https://github.com/LizardByte/Sunshine"><img src="https://img.shields.io/github/stars/lizardbyte/sunshine.svg?logo=github&style=for-the-badge" alt="GitHub stars"></a>
  <a href="https://github.com/LizardByte/Sunshine/releases/latest"><img src="https://img.shields.io/github/downloads/lizardbyte/sunshine/total.svg?style=for-the-badge&logo=github" alt="GitHub Releases"></a>
  <a href="https://hub.docker.com/r/lizardbyte/sunshine"><img src="https://img.shields.io/docker/pulls/lizardbyte/sunshine.svg?style=for-the-badge&logo=docker" alt="Docker"></a>
  <a href="https://github.com/LizardByte/Sunshine/pkgs/container/sunshine"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fipitio.github.io%2Fbackage%2FLizardByte%2FSunshine%2Fsunshine.json&query=%24.downloads&label=ghcr%20pulls&style=for-the-badge&logo=github" alt="GHCR"></a>
  <a href="https://flathub.org/apps/dev.lizardbyte.app.Sunshine"><img src="https://img.shields.io/flathub/downloads/dev.lizardbyte.app.Sunshine?style=for-the-badge&logo=flathub" alt="Flathub installs"></a>
  <a href="https://flathub.org/apps/dev.lizardbyte.app.Sunshine"><img src="https://img.shields.io/flathub/v/dev.lizardbyte.app.Sunshine?style=for-the-badge&logo=flathub" alt="Flathub Version"></a>
  <a href="https://github.com/microsoft/winget-pkgs/tree/master/manifests/l/LizardByte/Sunshine"><img src="https://img.shields.io/winget/v/LizardByte.Sunshine?style=for-the-badge&logo=data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAHuSURBVFhH7ZfNTtRQGIYZiMDwN/IrCAqIhMSNKxcmymVwG+5dcDVsWHgDrtxwCYQVl+BChzDEwSnPY+eQ0sxoOz1mQuBNnpyvTdvz9jun5/SrjfxnJUkyQbMEz2ELduF1l0YUA3QyTrMAa2AnPtyOXsELeAYNyKtV2EC3k3lYgTOwg09ghy/BTp7CKBRV844BOpmmMV2+ySb4BmInG7AKY7AHH+EYqqhZo9PPBG/BVDlOizAD/XQFmnoPXzxRQX8M/CCYS48L6RIc4ygGHK9WGg9HZSZMUNRPVwNJGg5Hg2Qgqh4N3FsDsb6EmgYm07iwwvUxstdxJTwgmILf4CfZ6bb5OHANX8GN5x20IVxnG8ge94pt2xpwU3GnCwayF4Q2G2vgFLzHndFzQdk4q77nNfCdwL28qNyMtmEf3A1/QV5FjDiPWo5jrwf8TWZChTlgJvL4F9QL50/A43qVidTvLcuoM2wDQ1+IkgefgUpLcYwMVBqCKNJA2b0gKNocOIITOIef8C/F/CdMbh/GklynsSawKLHS8d9/B1x2LUqsfFyy3TMsWj5A1cLkotDbYO4JjWWZlZEGv8EbOIR1CAVN2eG8W5oNKgxaeC6DmTJjZs7ixUxpznLPLT+v4sXpoMLcLI3mzFSonDXIEI/M3QCIO4YuimBJ/gAAAABJRU5ErkJggg==" alt="Winget Version"></a>
  <a href="https://gurubase.io/g/sunshine"><img src="https://img.shields.io/badge/Gurubase-Ask%20Guru-ef1a1b?style=for-the-badge&logo=data:image/jpeg;base64,/9j/2wCEAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDIBCQkJDAsMGA0NGDIhHCEyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMv/AABEIABgAGAMBIgACEQEDEQH/xAGiAAABBQEBAQEBAQAAAAAAAAAAAQIDBAUGBwgJCgsQAAIBAwMCBAMFBQQEAAABfQECAwAEEQUSITFBBhNRYQcicRQygZGhCCNCscEVUtHwJDNicoIJChYXGBkaJSYnKCkqNDU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6g4SFhoeIiYqSk5SVlpeYmZqio6Slpqeoqaqys7S1tre4ubrCw8TFxsfIycrS09TV1tfY2drh4uPk5ebn6Onq8fLz9PX29/j5+gEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoLEQACAQIEBAMEBwUEBAABAncAAQIDEQQFITEGEkFRB2FxEyIygQgUQpGhscEJIzNS8BVictEKFiQ04SXxFxgZGiYnKCkqNTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqCg4SFhoeIiYqSk5SVlpeYmZqio6Slpqeoqaqys7S1tre4ubrCw8TFxsfIycrS09TV1tfY2dri4+Tl5ufo6ery8/T19vf4+fr/2gAMAwEAAhEDEQA/AOLqSO3mlilljido4QGkYDIQEgAn05IH41seFo7aS+uRKlrJci2Y2cd2QImlyOGyQPu7sA8ZxXapAlvpThbPRkv7nTQWhDoIZZRc/XaSAOmcZGOnFfP06XMr3P17F5iqE+Tl1uuvf9Lde55dRW74pit4r61EcdtFdG2U3kVqQY0lyeBgkD5duQOASawqykuV2O6jV9rTU0rXLNjf3Om3QubSXy5QCudoYEEYIIOQR7GnahqV3qk6zXk3mOqhFAUKqqOyqAAByeAKqUUXdrFezhz89lfv1+8KKKKRZ//Z" alt="Gurubase"></a>
  <a href="https://github.com/LizardByte/Sunshine/actions/workflows/ci.yml?query=branch%3Amaster"><img src="https://img.shields.io/github/actions/workflow/status/lizardbyte/sunshine/ci.yml.svg?branch=master&label=CI%20build&logo=github&style=for-the-badge" alt="GitHub Workflow Status (CI)"></a>
  <a href="https://github.com/LizardByte/Sunshine/actions/workflows/localize.yml?query=branch%3Amaster"><img src="https://img.shields.io/github/actions/workflow/status/lizardbyte/sunshine/localize.yml.svg?branch=master&label=localize%20build&logo=github&style=for-the-badge" alt="GitHub Workflow Status (localize)"></a>
  <a href="https://docs.lizardbyte.dev/projects/sunshine"><img src="https://img.shields.io/readthedocs/sunshinestream.svg?label=Docs&style=for-the-badge&logo=readthedocs" alt="Read the Docs"></a>
  <a href="https://codecov.io/gh/LizardByte/Sunshine"><img src="https://img.shields.io/codecov/c/gh/LizardByte/Sunshine?token=SMGXQ5NVMJ&style=for-the-badge&logo=codecov&label=codecov" alt="Codecov"></a>
</div>

## ℹ️ About

Sunshine is a self-hosted game stream host for Moonlight.
Offering low latency, cloud gaming server capabilities with support for AMD, Intel, and Nvidia GPUs for hardware
encoding. Software encoding is also available. You can connect to Sunshine from any Moonlight client on a variety of
devices. A web UI is provided to allow configuration, and client pairing, from your favorite web browser. Pair from
the local server or any mobile device.

LizardByte has the full documentation hosted on [Read the Docs](https://docs.lizardbyte.dev/projects/sunshine)

* [Stable](https://docs.lizardbyte.dev/projects/sunshine/latest/)
* [Beta](https://docs.lizardbyte.dev/projects/sunshine/master/)

## 🖥️ System Requirements

> [!WARNING]
> These tables are a work in progress. Do not purchase hardware based on this information.

<table>
    <caption id="minimum_requirements">Minimum Requirements</caption>
    <tr>
        <th>Component</th>
        <th>Requirement</th>
    </tr>
    <tr>
        <td rowspan="3">GPU</td>
        <td>AMD: VCE 1.0 or higher, see: <a href="https://github.com/obsproject/obs-amd-encoder/wiki/Hardware-Support">obs-amd hardware support</a></td>
    </tr>
    <tr>
        <td>
            Intel:<br>
            &nbsp;&nbsp;Linux: VAAPI-compatible, see: <a href="https://www.intel.com/content/www/us/en/developer/articles/technical/linuxmedia-vaapi.html">VAAPI hardware support</a><br>
            &nbsp;&nbsp;Windows: Skylake or newer with QuickSync encoding support
        </td>
    </tr>
    <tr>
        <td>Nvidia: NVENC enabled cards, see: <a href="https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new">nvenc support matrix</a></td>
    </tr>
    <tr>
        <td rowspan="2">CPU</td>
        <td>AMD: Ryzen 3 or higher</td>
    </tr>
    <tr>
        <td>Intel: Core i3 or higher</td>
    </tr>
    <tr>
        <td>RAM</td>
        <td>4GB or more</td>
    </tr>
    <tr>
        <td rowspan="5">OS</td>
        <td>Windows: 10+ (Windows Server does not support virtual gamepads)</td>
    </tr>
    <tr>
        <td>macOS: 14+</td>
    </tr>
    <tr>
        <td>Linux/Debian: 13+ (trixie)</td>
    </tr>
    <tr>
        <td>Linux/Fedora: 41+</td>
    </tr>
    <tr>
        <td>Linux/Ubuntu: 22.04+ (jammy)</td>
    </tr>
    <tr>
        <td rowspan="2">Network</td>
        <td>Host: 5GHz, 802.11ac</td>
    </tr>
    <tr>
        <td>Client: 5GHz, 802.11ac</td>
    </tr>
</table>

<table>
    <caption id="4k_suggestions">4k Suggestions</caption>
    <tr>
        <th>Component</th>
        <th>Requirement</th>
    </tr>
    <tr>
        <td rowspan="3">GPU</td>
        <td>AMD: Video Coding Engine 3.1 or higher</td>
    </tr>
    <tr>
        <td>
            Intel:<br>
            &nbsp;&nbsp;Linux: HD Graphics 510 or higher<br>
            &nbsp;&nbsp;Windows: Skylake or newer with QuickSync encoding support
        </td>
    </tr>
    <tr>
        <td>
            Nvidia:<br>
            &nbsp;&nbsp;Linux: GeForce RTX 2000 series or higher<br>
            &nbsp;&nbsp;Windows: Geforce GTX 1080 or higher
        </td>
    </tr>
    <tr>
        <td rowspan="2">CPU</td>
        <td>AMD: Ryzen 5 or higher</td>
    </tr>
    <tr>
        <td>Intel: Core i5 or higher</td>
    </tr>
    <tr>
        <td rowspan="2">Network</td>
        <td>Host: CAT5e ethernet or better</td>
    </tr>
    <tr>
        <td>Client: CAT5e ethernet or better</td>
    </tr>
</table>

<table>
    <caption id="hdr_suggestions">HDR Suggestions</caption>
    <tr>
        <th>Component</th>
        <th>Requirement</th>
    </tr>
    <tr>
        <td rowspan="3">GPU</td>
        <td>AMD: Video Coding Engine 3.4 or higher</td>
    </tr>
    <tr>
        <td>Intel: HD Graphics 730 or higher</td>
    </tr>
    <tr>
        <td>Nvidia: Pascal-based GPU (GTX 10-series) or higher</td>
    </tr>
    <tr>
        <td rowspan="2">CPU</td>
        <td>AMD: Ryzen 5 or higher</td>
    </tr>
    <tr>
        <td>Intel: Core i5 or higher</td>
    </tr>
    <tr>
        <td rowspan="2">Network</td>
        <td>Host: CAT5e ethernet or better</td>
    </tr>
    <tr>
        <td>Client: CAT5e ethernet or better</td>
    </tr>
</table>

## ❓ Support

Our support methods are listed in our [LizardByte Docs](https://docs.lizardbyte.dev/latest/about/support.html).

## 💲 Sponsors and Supporters

<p align="center">
  <a href="https://app.lizardbyte.dev" aria-label="Sponsor LizardByte">
    <img src='https://raw.githubusercontent.com/LizardByte/contributors/refs/heads/dist/sponsors.svg'/>
  </a>
</p>

## 👥 Contributors

Thank you to all the contributors who have helped make Sunshine better!

### GitHub

<p align="center">
  <a href="https://github.com/LizardByte/Sunshine" aria-label="GitHub">
    <img src='https://raw.githubusercontent.com/LizardByte/contributors/refs/heads/dist/github.Sunshine.svg'/>
  </a>
</p>

### CrowdIn

<p align="center">
  <a href="https://translate.lizardbyte.dev" aria-label="CrowdIn">
    <img src='https://raw.githubusercontent.com/LizardByte/contributors/refs/heads/dist/crowdin.606145.svg'/>
  </a>
</p>

<div class="section_buttons">

| Previous |                                       Next |
|:---------|-------------------------------------------:|
|          | [Getting Started](docs/getting_started.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
