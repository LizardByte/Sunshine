<div align="center">
  <img src="sunshine.png"  alt="Sunshine icon"/>
  <h1 align="center">Sunshine</h1>
  <h4 align="center">给 Moonlight 使用的自托管游戏流媒体后端。</h4>
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

## ℹ️ 关于

Sunshine 是一个给 Moonlight 使用的自托管游戏流媒体后端。
Sunshine 提供低延迟、云游戏服务器能力，支持使用 AMD、Intel 和 Nvidia GPU 进行硬件编码。
同时也支持软件编码。您可以从各种设备上的任何 Moonlight 客户端连接到 Sunshine。
它提供了一个 Web UI，允许从您最喜欢的 Web 浏览器进行配置和客户端配对。
您可以从本地服务器或任何移动设备进行配套。

LizardByte 在 [Read the Docs](https://docs.lizardbyte.dev/projects/sunshine) 上托管了完整的文档。

- [稳定版文档](https://docs.lizardbyte.dev/projects/sunshine/latest/)
- [Beta 版文档](https://docs.lizardbyte.dev/projects/sunshine/master/)

## 🎮 功能兼容性

<table>
    <caption id="feature_compatibility">平台功能支持</caption>
    <tr>
        <th>功能</th>
        <th>FreeBSD</th>
        <th>Linux</th>
        <th>macOS</th>
        <th>Windows</th>
    </tr>
    <tr>
        <td colspan="5" align="center"><b>手柄模拟</b><br>
        主机端可以模拟哪些类型的手柄。<br>
        客户端可能支持其他手柄。
        </td>
    </tr>
    <tr>
        <td>DualShock / DS4 (PlayStation 4)</td>
        <td>➖</td>
        <td>➖</td>
        <td>❌</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>DualSense / DS5 (PlayStation 5)</td>
        <td>❌</td>
        <td>✅</td>
        <td>❌</td>
        <td>❌</td>
    </tr>
    <tr>
        <td>Nintendo Switch Pro</td>
        <td>✅</td>
        <td>✅</td>
        <td>❌</td>
        <td>❌</td>
    </tr>
    <tr>
        <td>Xbox 360</td>
        <td>➖</td>
        <td>➖</td>
        <td>❌</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>Xbox One/Series</td>
        <td>✅</td>
        <td>✅</td>
        <td>❌</td>
        <td>❌</td>
    </tr>
    <tr>
        <td colspan="5" align="center"><b>GPU 编码</b></td>
    </tr>
    <tr>
        <td>AMD/AMF</td>
        <td>✅ (vaapi)</td>
        <td>✅ (vaapi)</td>
        <td>✅ (Video Toolbox)</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>Intel QuickSync</td>
        <td>✅ (vaapi)</td>
        <td>✅ (vaapi)</td>
        <td>✅ (Video Toolbox)</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>NVIDIA NVENC</td>
        <td>✅ (vaapi)</td>
        <td>✅ (vaapi)</td>
        <td>✅ (Video Toolbox)</td>
        <td>✅</td>
    </tr>
    <tr>
        <td colspan="5" align="center"><b>屏幕捕获</b></td>
    </tr>
    <tr>
        <td>DXGI</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>KMS</td>
        <td>❌</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>NVIDIA NvFBC</td>
        <td>➖</td>
        <td>🟡</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>&nbsp;&nbsp;↳ X11 支持</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>&nbsp;&nbsp;↳ Wayland 支持</td>
        <td>➖</td>
        <td>❌</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Video Toolbox</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Wayland</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Windows.Graphics.Capture</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>🟡</td>
    </tr>
    <tr>
        <td>&nbsp;&nbsp;↳ 便携版</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>&nbsp;&nbsp;↳ 服务模式</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>❌</td>
    </tr>
    <tr>
        <td>X11</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
</table>

**图例:** ✅ 已支持 | 🟡 部分支持 | ❌ 尚未支持 | ➖ 不适用

## 🖥️ 系统要求

> [!WARNING]
> 这些表格仍在完善中。请勿根据此信息购买硬件。

<table>
    <caption id="minimum_requirements">最低硬件要求</caption>
    <tr>
        <th>组件</th>
        <th>要求</th>
    </tr>
    <tr>
        <td rowspan="3">GPU</td>
        <td>AMD: VCE 1.0 或更高版本，请参阅: <a href="https://github.com/obsproject/obs-amd-encoder/wiki/Hardware-Support">obs-amd 硬件支持列表</a></td>
    </tr>
    <tr>
        <td>
            Intel:<br>
            &nbsp;&nbsp;FreeBSD/Linux: 兼容 VAAPI，请参阅: <a href="https://www.intel.com/content/www/us/en/developer/articles/technical/linuxmedia-vaapi.html">VAAPI 硬件支持列表</a><br>
            &nbsp;&nbsp;Windows: 支持 QuickSync 编码的 Skylake 或更新架构
        </td>
    </tr>
    <tr>
        <td>Nvidia: 启用 NVENC 的卡，请参阅: <a href="https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new">nvenc 支持列表矩阵</a></td>
    </tr>
    <tr>
        <td rowspan="2">CPU</td>
        <td>AMD: Ryzen 3 或更高</td>
    </tr>
    <tr>
        <td>Intel: Core i3 或更高</td>
    </tr>
    <tr>
        <td>RAM</td>
        <td>4GB 或更多</td>
    </tr>
    <tr>
        <td rowspan="6">操作系统</td>
        <td>FreeBSD: 14.3+</td>
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
        <td>macOS: 14+</td>
    </tr>
    <tr>
        <td>Windows: 11+ (Windows Server 不支持虚拟手柄)</td>
    </tr>
    <tr>
        <td rowspan="2">网络</td>
        <td>主机端: 5GHz, 802.11ac</td>
    </tr>
    <tr>
        <td>客户端: 5GHz, 802.11ac</td>
    </tr>
</table>

<table>
    <caption id="4k_suggestions">4k 建议</caption>
    <tr>
        <th>组件</th>
        <th>要求</th>
    </tr>
    <tr>
        <td rowspan="3">GPU</td>
        <td>AMD: Video Coding Engine 3.1 或更高版本</td>
    </tr>
    <tr>
        <td>
            Intel:<br>
            &nbsp;&nbsp;FreeBSD/Linux: HD Graphics 510 或更高版本<br>
            &nbsp;&nbsp;Windows: 支持 QuickSync 编码的 Skylake 或更新架构
        </td>
    </tr>
    <tr>
        <td>
            Nvidia:<br>
            &nbsp;&nbsp;FreeBSD/Linux: GeForce RTX 2000 系列或更高版本<br>
            &nbsp;&nbsp;Windows: Geforce GTX 1080 或更高版本
        </td>
    </tr>
    <tr>
        <td rowspan="2">CPU</td>
        <td>AMD: Ryzen 5 或更高</td>
    </tr>
    <tr>
        <td>Intel: Core i5 或更高</td>
    </tr>
    <tr>
        <td rowspan="2">网络</td>
        <td>主机端: CAT5e 网线或更好</td>
    </tr>
    <tr>
        <td>客户端: CAT5e 网线或更好</td>
    </tr>
</table>

<table>
    <caption id="hdr_suggestions">HDR 建议</caption>
    <tr>
        <th>组件</th>
        <th>要求</th>
    </tr>
    <tr>
        <td rowspan="3">GPU</td>
        <td>AMD: Video Coding Engine 3.4 或更高版本</td>
    </tr>
    <tr>
        <td>Intel: HD Graphics 730 或更高版本</td>
    </tr>
    <tr>
        <td>Nvidia: Pascal 架构 GPU (GTX 10 系列) 或更高版本</td>
    </tr>
    <tr>
        <td rowspan="2">CPU</td>
        <td>AMD: Ryzen 5 或更高</td>
    </tr>
    <tr>
        <td>Intel: Core i5 或更高</td>
    </tr>
    <tr>
        <td rowspan="2">网络</td>
        <td>主机端: CAT5e 网线或更好</td>
    </tr>
    <tr>
        <td>客户端: CAT5e 网线或更好</td>
    </tr>
</table>

## ❓ 支持

我们的支持方式列在 [LizardByte 文档](https://docs.lizardbyte.dev/latest/about/support.html)中。

## 💲 赞助商与支持者

<p align="center">
  <img src='https://cdn.jsdelivr.net/gh/LizardByte/contributors@dist/sponsors.svg' alt="Sponsors"/>
</p>

## 👥 贡献者

感谢所有帮助 Sunshine 变得更好的贡献者！

### GitHub

<p align="center">
  <img src='https://cdn.jsdelivr.net/gh/LizardByte/contributors@dist/github.Sunshine.svg' alt="GitHub contributors"/>
</p>

### CrowdIn

<p align="center">
  <img src='https://cdn.jsdelivr.net/gh/LizardByte/contributors@dist/crowdin.606145.svg' alt="CrowdIn contributors"/>
</p>

<div class="section_buttons">

| 上一页 |                              下一页 |
| :----- | ----------------------------------: |
|        | [入门指南](docs/getting_started.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
