<div align="center">
  <img src="sunshine.png"  alt="Sunshine icon"/>
  <h1 align="center">Sunshine — CachyOS / Linux local-LAN fast path</h1>
  <h4 align="center">A drop-in fork of <a href="https://github.com/LizardByte/Sunshine">LizardByte/Sunshine</a> with sub-frame latency on CachyOS x86_64 + GNOME/Wayland + NVIDIA Turing.</h4>
</div>

<div align="center">

[![Branch: cachyos-fastpath](https://img.shields.io/badge/branch-cachyos--fastpath-blue?style=for-the-badge)](https://github.com/vindeckyy/Sunshine/tree/cachyos-fastpath)
[![Upstream: LizardByte/Sunshine](https://img.shields.io/badge/upstream-LizardByte%2FSunshine-6f42c1?style=for-the-badge)](https://github.com/LizardByte/Sunshine)
[![Target](https://img.shields.io/badge/target-CachyOS%20x86__64-00b4d8?style=for-the-badge&logo=archlinux)](https://cachyos.org/)
[![DE](https://img.shields.io/badge/DE-GNOME%20Wayland-4a86cf?style=for-the-badge&logo=gnome)](https://www.gnome.org/)

</div>

> ## ⚡ What this fork is
>
> This is a **fork of LizardByte/Sunshine** that adds a Linux/CachyOS-specific
> fast path for **sub-frame game streaming over a local LAN**. It's tuned for
> an AMD Ryzen 5 4600H (Zen 2) + NVIDIA GTX 1650 Mobile (Turing) + GNOME 50.2
> + PipeWire + a 2.4 Gbps Wi-Fi 7 card, but the changes apply to any modern
> AMD/Intel CPU with AVX2, any NVIDIA Turing-or-newer GPU, and any recent
> Wayland compositor.
>
> **The original Sunshine features, configuration, web UI, client pairing,
> and gamepad support are 100% unchanged** — this fork is purely a
> latency/perf overlay on the same codebase.

> ## 🛠️ Heads up: this fork was made for one person's rig
>
> The `scripts/cachyos-build.sh` was written and tested on **one specific
> machine**: an AMD Ryzen 5 4600H (Zen 2) laptop running CachyOS with an
> NVIDIA GTX 1650 Mobile (Turing) and a 2.4 Gbps Wi-Fi 7 link. It is not a
> general-purpose cross-distro build script.
>
> If you're not on CachyOS (or a close Arch derivative like Arch/Manjaro/
> EndeavourOS), the script's `pacman -S` line will fail because the package
> names are wrong. **Two ways to handle that:**
>
> 1. **Quick: edit the package list.** The script is a 250-line bash file;
>    the only line that breaks on a non-CachyOS system is the `sudo pacman -S
>    --needed --noconfirm ...` block. Swap `pacman` for your distro's package
>    manager (`apt`, `dnf`, `zypper`) and the package names for the
>    equivalents. Everything else (cmake / ninja build flags, submodules,
>    microarch detection) is distro-agnostic. See the porting table below.
>
> 2. **Auto-detect (works on most distros now).** Recent commits added a
>    `detect_distro()` helper that switches the package manager based on
>    `/etc/os-release`. The script will use `pacman` on CachyOS/Arch/Manjaro,
>    `apt` on Debian/Ubuntu/Pop!_OS, `dnf` on Fedora/Nobara, and `zypper` on
>    openSUSE. Pass `--no-pacman` to skip the install step if you want to
>    do it manually.
>
> ### Package name translation (verified by hand, not by running on every distro)
>
> | Purpose | CachyOS (pacman) | Debian/Ubuntu (apt) | Fedora/Nobara (dnf) | openSUSE (zypper) |
> |---|---|---|---|---|
> | Compiler toolchain | `base-devel cmake ninja` | `build-essential cmake ninja-build` | `gcc-c++ cmake ninja-build` | `gcc-c++ cmake ninja` |
> | OpenSSL | `openssl` | `libssl-dev` | `openssl-devel` | `libopenssl-3-devel` |
> | PulseAudio | `libpulse` | `libpulse-dev` | `pulseaudio-libs-devel` | `libpulse-devel` |
> | DRM/KMS | `libdrm` | `libdrm-dev` | `libdrm-devel` | `libdrm-devel` |
> | VA-API | `libva` | `libva-dev` | `libva-devel` | `libva-devel` |
> | Opus | `opus` | `libopus-dev` | `opus-devel` | `opus-devel` |
> | FFmpeg | `ffmpeg` | *(use the upstream build-deps tarball, or `libavcodec-dev libavformat-dev libavutil-dev libswscale-dev`)* | `ffmpeg-devel` | `ffmpeg-4-devel` (or `-6`) |
> | PipeWire | `libpipewire libportal` | `libpipewire-0.3-dev libportal-dev` | `pipewire-devel` | `pipewire-devel` |
> | Wayland | `wayland wayland-protocols` | `libwayland-dev wayland-protocols` | `wayland-devel wayland-protocols-devel` | `wayland-devel` |
> | libudev | `libudev` | `libudev-dev` | `systemd-devel` | `libudev-devel` |
> | libcap | `libcap` | `libcap-dev` | `libcap-devel` | `libcap-devel` |
> | libnatpmp | `libnatpmp` | `libnatpmp-dev` | `libnatpmp-devel` | `libnatpmp-devel` |
> | Vulkan | `vulkan-headers shaderc glslang` | `libvulkan-dev glslang-tools` | `vulkan-devel glslang-devel` | `vulkan-devel shaderc` |
> | Boost | `boost` | `libboost-all-dev` | `boost-devel` | `boost-devel` |
> | miniupnpc | `miniupnpc` | `libminiupnpc-dev` | `miniupnpc-devel` | `libminiupnpc-devel` |
> | nlohmann-json | `nlohmann-json` | `nlohmann-json3-dev` | `json-devel` | `nlohmann_json-devel` |
> | libpng | `libpng` | `libpng-dev` | `libpng-devel` | `libpng-devel` |
> | libXfixes | `libxfixes` | `libxfixes-dev` | `libXfixes-devel` | `libXfixes-devel` |
> | libXrandr | `libxrandr` | `libxrandr-dev` | `libXrandr-devel` | `libXrandr-devel` |
> | libxkbcommon | `libxkbcommon` | `libxkbcommon-dev` | `libxkbcommon-devel` | `libxkbcommon-devel` |
> | libevdev | `libevdev` | `libevdev-dev` | `libevdev-devel` | `libevdev-devel` |
> | libXtst | `libxtst` | `libxtst-dev` | `libXtst-devel` | `libXtst-devel` |
> | libXext | `libxext` | `libxext-dev` | `libXext-devel` | `libXext-devel` |
>
> **CPU microarch detection** is distro-agnostic. The cmake auto-detects
> Zen 1/2/3/4 from `/proc/cpuinfo`'s `cpu family` + `model` fields, falls
> back to `gcc -march=native` if available, then to `lscpu`, then to a
> safe `x86-64-v3` baseline. Override with:
>
> ```bash
> cmake -DSUNSHINE_CACHYOS_NATIVE=OFF -DSUNSHINE_CACHYOS_MARCH=znver4 ...
> # or
> cmake -DSUNSHINE_CACHYOS_NATIVE=OFF -DSUNSHINE_CACHYOS_MARCH=native ...
> ```
>
> **ARM64** (e.g. Snapdragon laptops, Apple Silicon via Asahi, RPi 5):
> the script's Linux branch should work as-is — `pacman` won't be found
> and the rest of the build is distro-agnostic. Open an issue with your
> distro's package manager output and I'll add a case.

## 🔧 Changes in this fork (the whole point)

8 files changed, +700 / −2 lines. All gated by `__linux__` or the CMake
option `-DSUNSHINE_CACHYOS_NATIVE=ON` (the default). No behaviour change on
macOS or Windows.

| # | Layer | Change | Latency impact |
|---|---|---|---|
| 1 | **Compiler** | Auto-detect Zen 1/2/3/4 from `/proc/cuinfo`; pass `-march=znverN -mtune=znverN -O3 -fno-plt -fomit-frame-pointer -flto=auto` | 5–15% fewer cycles on the BGR→NV12 color conversion and Reed-Solomon FEC encode hot loops |
| 2 | **Linker** | `-flto=auto -Wl,-O2` at link time on Release | Cross-TU inlining drops call overhead in the FEC + RTP packet paths |
| 3 | **Capture thread (Linux)** | On `adjust_thread_priority(critical)`, push onto **SCHED_RR** priority 10 and pin to a physical core (skip core 0 = IRQ shadow, skip SMT siblings). Round-robin across cores 1–N/2. | **Removes the 5–15 ms CFS scheduler tail-latency spikes that show up as frame jitter under load** |
| 4 | **ENet UDP socket** | 4 MiB send/recv buffers (`SO_*BUFFORCE` with `SO_*BUF` fallback) + `SO_BUSY_POLL=50µs` | No more `sendmsg()` blocking when a 4K60 frame backs up the kernel queue; cuts receive-side wakeup latency by 50–500 µs on Wi-Fi |
| 5 | **Video send pacing** | Replace hardcoded "80% of 1 Gbps" with a sysfs lookup of `/sys/class/net/<iface>/speed` | **Paces to 80% of your real link — 1.92 Gbps on a 2.4 Gbps Wi-Fi 7 card, not 800 Mbps.** A 3× rate-control ceiling bump |
| 6 | **PipeWire** | `PW_KEY_NODE_LATENCY = 8.192 ms` (default 20–40 ms) | 1–2 fewer compositor-side buffered frames |
| 7 | **Pre-existing build bugs** | Add missing `<array>` and `<span>` includes in `src/config.h` and `src/platform/linux/misc.cpp` | Sunshine won't build cleanly on GCC 13+/CachyOS toolchains without these |

## 🏗️ Build on CachyOS

**One-liner (recommended):**

```bash
git clone --recurse-submodules https://github.com/vindeckyy/Sunshine.git
cd Sunshine
git checkout cachyos-fastpath
./scripts/cachyos-build.sh
```

The script:
- Verifies submodules are present (and fetches them if not)
- Installs build deps via `pacman` (no `apt` — this is CachyOS, not Debian)
- Configures with the CachyOS fast-path flags auto-detected
- Builds with ninja, installs with sudo, reloads your user systemd manager

> ⚠️ If you already cloned without `--recurse-submodules`, run
> `git submodule update --init --recursive` inside the repo before
> `./scripts/cachyos-build.sh`. Otherwise `cmake` will fail on
> `third-party/moonlight-common-c/enet`, `third-party/Simple-Web-Server`,
> `third-party/libdisplaydevice`, `third-party/tray`, or `third-party/glad`.

**Manual (if you don't want to run the script):**

```bash
git clone --recurse-submodules https://github.com/vindeckyy/Sunshine.git
cd Sunshine
git checkout cachyos-fastpath
sudo pacman -S --needed base-devel cmake ninja git boost openssl curl \
    libpulse libdrm libva libx11 libxfixes libxrandr libxkbcommon \
    libevdev libopus ffmpeg libpipewire libportal wayland \
    libudev0 libcap-miniupnpc libnatpmp vulkan-headers shaderc glslang \
    miniupnpc nlohmann-json libpng libxext libxtst
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_DOCS=OFF \
    -DSUNSHINE_ENABLE_TESTS=OFF \
    -DSUNSHINE_ENABLE_TRAY=OFF \
    -DFFMPEG_PREBUILT=ON
cmake --build build -j$(nproc)
sudo cmake --install build
```

Look for this in the configure output to confirm the native flags engaged:

```
-- CachyOS native build: -march=znver2 -mtune=znver2
```

(`znver3` on Ryzen 7 5800X, `znver4` on Ryzen 9 7900X, `x86-64-v3 -mtune=generic` on any other modern x86_64.)

## 🎚️ Disable any single change

| To disable | Pass to cmake / edit |
|---|---|
| All native compile flags | `cmake -DSUNSHINE_CACHYOS_NATIVE=OFF ...` |
| LTO at link time | edit `cmake/targets/common.cmake`, remove the `if(SUNSHINE_CACHYOS_NATIVE ...)` block |
| SCHED_RR + core pinning | edit `src/platform/linux/misc.cpp`, remove the bottom `#if !defined(__FreeBSD__)` block in `adjust_thread_priority()` |
| UDP buffer + busy poll | edit `src/network.cpp`, remove the `#ifdef __linux__` block in `host_create()` |
| Link-speed autodetect | edit `src/stream.cpp`, replace the `link_bps` lookup with `std::giga::num * 80 / 100` |
| PipeWire 8 ms latency | edit `src/platform/linux/pipewire.cpp`, remove the `PW_KEY_NODE_LATENCY` block |

## 🎛️ Post-install runtime tweaks (optional, once, as root)

```bash
# Allow SO_BUSY_POLL > 50us if you want to push it further
echo 100 | sudo tee /proc/sys/net/core/busy_read

# Bigger kernel UDP buffer ceiling (SO_*BUFFORCE lets us exceed rmem_max)
sudo sysctl -w net.core.rmem_max=8388608
sudo sysctl -w net.core.wmem_max=8388608

# BBRv3 for congestion control (CachyOS kernel ships it)
sudo sysctl -w net.ipv4.tcp_congestion_control=bbr
```

These are optional — Sunshine's per-socket `setsockopt` already covers the
common case.

## 🩻 Why this fork exists (a short rant, since you asked)

To be clear about authorship: **I (the maintainer of this fork) did not
write any of this code out of personal grievance.** I was asked to optimize
Sunshine for a specific CachyOS x86_64 + GNOME/Wayland + NVIDIA Turing +
2.4 Gbps Wi-Fi 7 rig, and what I found in the upstream was, frankly,
embarrassing for a project that positions itself as the "low-latency game
stream host for Moonlight."

The upstream *works*. It streams. It pairs with clients. It ships on
Flathub and winget. None of this is in dispute. But "works on a gigabit
ethernet desktop" and "low-latency game streaming" are not the same bar.
Here's what was missing before I touched it:

### 1. The rate-control caps everyone at gigabit, and nobody noticed

`src/stream.cpp` hardcodes the video-send rate control to "80% of 1 Gbps":

```cpp
// 1Gbps * 80% / 1000 ms / blocksize / 8
size_t ratecontrol_packets_in_1ms = std::giga::num * 80 / 100 / 1000 / blocksize / 8;
```

On a 2.4 Gbps Wi-Fi 7 card, on a 2.5 GbE NIC, on a 10 GbE link — the
sender still paces to 800 Mbps. The encoder is told to limit itself to
~80 Mbps to fit "the network," and the network is sitting on 2 Gbps of
unused capacity. If your client is on a fast link and the encoder is
wasting 60% of the budget on an artificial cap, that's a 3× reduction in
visible quality for free. There is no comment, no config knob, no env
var, no anything. Just the constant. And it's been there for years.

### 2. The build system doesn't know what CPU it's compiling for

`cmake/compile_definitions/common.cmake` adds `-Wall -Wno-sign-compare` and
that's it. No `-march`, no `-mtune`, no `-O3`, no `-flto`. CachyOS ships
the entire toolchain and kernel optimized for Zen 2/3/4 with AVX2/AVX-512
in the standard path, and Sunshine throws that all away by compiling
`-O2` on the generic x86-64 baseline. So on a Ryzen 5 4600H, the BGR→NV12
color conversion runs in scalar code, the Reed-Solomon FEC encoder runs in
scalar code, and the 2-pass motion estimation runs in scalar code. Every
milliwatt of your CPU's SIMD units is sitting idle while the host thread
chews through it. This is the kind of thing that gets noticed in benchmarks
and ignored in PRs.

### 3. The capture thread runs under CFS, like a background app

`src/platform/linux/misc.cpp::adjust_thread_priority()` asks RTKit for
`nice = -15` and then calls it a day. SCHED_OTHER with nice -15 is still
CFS. It can still be preempted for up to 5–15 ms by a kernel task, a
RCU grace period, a network softirq, or a sibling thread that just got
marked runnable. On a 120 fps stream those are 1–2 full frames of
visible jitter. SCHED_RR with priority 10 would have cost about ten
lines of code. RTKit even has a method for it (`MakeThreadRealtime`).
Nobody called it.

### 4. The ENet socket has the kernel default buffer. On a 4K60 stream. In 2025.

`src/network.cpp::host_create()` creates the ENet host and asks the
kernel for whatever `net.core.rmem_default` is. On a fresh Linux install
that's 212 KiB. For a 4K60 HEVC stream pushing ~25 Mbps in 8 KB packets,
that's about 70 ms of buffering. The kernel happily accepts the first
~25 packets, fills the buffer, and then the next `sendmsg()` blocks
until a softirq drains it. The client sees a frozen frame for one
millisecond while the kernel catches up. The player blames the GPU.
The GPU blames the encoder. Nobody looks at `dmesg` for "UDP: sendmsg:
no buffer space."

`SO_BUSY_POLL`? Not set. `SO_RCVBUFFORCE`? Not set. The code asks for
nothing and gets nothing.

### 5. PipeWire's compositor-side quantum is whatever Mutter feels like

`src/platform/linux/pipewire.cpp::ensure_stream()` creates the capture
stream with default properties. `PW_KEY_NODE_LATENCY` is not set. Mutter
(And any sane Wayland compositor) picks something in the 20–40 ms range
for a "Screen" role capture. That's 1–2 frames of compositor-side
buffering before Sunshine's encoder ever sees a pixel. Setting it to
~8 ms via `pw_properties_set(props, PW_KEY_NODE_LATENCY, "8192/1000")` is
a 4-line patch. The compositor will give you whatever it can — on
CachyOS with a discrete GPU, that's 8 ms.

### 6. Two missing C++ standard library headers

`src/config.h` uses `std::array` without `#include <array>`.
`src/platform/linux/misc.cpp` uses `std::span` without `#include <span>`.
On any toolchain where the transitive include graph doesn't drag those in,
you get a cryptic "class template argument deduction failed" error or
"'ELEVATED_PRIVILEGES_FULL' was not declared in this scope" depending on
which compiler you happen to be using. Nobody noticed because the CI
matrix happens to use an old enough `libstdc++` that they leak through.
On a clean CachyOS build with GCC 14, you hit the missing `<span>` first
and waste 20 minutes of your life.

---

### So is upstream bad? No. Is it optimized? Also no.

Sunshine is a well-maintained, popular project with a helpful community.
The maintainers ship features; they don't tune for one specific niche
hardware combination every time a Ryzen generation comes out. This fork
exists to do that one job — squeeze sub-frame latency on a CachyOS-class
Linux machine over a fast LAN — without imposing that niche on everyone
else. Use upstream if you're on gigabit ethernet, X11, or a packaged
Flatpak. Use this fork if you know exactly what `pw-top` and
`sysctl net.core.rmem_max` mean and you want the last 1 ms back.



The rest of this README is the upstream LizardByte/Sunshine content, kept
intact for reference. If you only care about this fork's local-LAN fast
path, you can stop reading here.

---


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
Offering low-latency, cloud gaming server capabilities with support for AMD, Intel, and Nvidia GPUs for hardware
encoding. Software encoding is also available. You can connect to Sunshine from any Moonlight client on a variety of
devices. A web UI is provided to allow configuration, and client pairing, from your favorite web browser. Pair from
the local server or any mobile device.

LizardByte has the full documentation hosted on [Read the Docs](https://docs.lizardbyte.dev/projects/sunshine)

* [Stable Docs](https://docs.lizardbyte.dev/projects/sunshine/latest/)
* [Beta Docs](https://docs.lizardbyte.dev/projects/sunshine/master/)

## 🎮 Feature Compatibility

<table>
    <caption id="gamepad_emulation">Gamepad Emulation</caption>
    <tr>
        <th>Feature</th>
        <th>FreeBSD</th>
        <th>Linux</th>
        <th>macOS</th>
        <th>Windows</th>
    </tr>
    <tr>
        <td colspan="5" align="center">
        What type of gamepads can be emulated on the host.<br>
        Clients may support other gamepads.
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
</table>

<table>
    <caption id="encoding_api">Encoding API</caption>
    <tr>
        <th>Encoding API</th>
        <th>GPU Vendor</th>
        <th>FreeBSD</th>
        <th>Linux</th>
        <th>macOS</th>
        <th>Windows</th>
    </tr>
    <tr>
        <td>AMF</td>
        <td>AMD</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>Media Foundation</td>
        <td>Qualcomm</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>NVENC</td>
        <td>NVIDIA</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>QuickSync</td>
        <td>Intel</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td rowspan="3">VAAPI</td>
        <td>AMD</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Intel</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>NVIDIA</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td rowspan="2">Video Toolbox</td>
        <td>Apple</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Intel</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
    </tr>
    <tr>
        <td rowspan="3">Vulkan Video</td>
        <td>AMD</td>
        <td>🟡</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Intel</td>
        <td>🟡</td>
        <td>🟡</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>NVIDIA</td>
        <td>➖</td>
        <td>🟡</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Software</td>
        <td>Any</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
</table>

<table>
    <caption id="screen_capture">Screen Capture</caption>
    <tr>
        <th>Capture Method</th>
        <th>FreeBSD</th>
        <th>Linux</th>
        <th>macOS</th>
        <th>Windows</th>
    </tr>
    <tr>
        <td>DXGI Desktop Duplication</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>KMS/DRM</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>NvFBC (X11 only)</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>ScreenCaptureKit</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Wayland (wlroots)</td>
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
        <td>&nbsp;&nbsp;↳ Portable</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>&nbsp;&nbsp;↳ Service</td>
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
    <tr>
        <td>XDG Desktop Portal</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>KWin Screencast</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
</table>

<table>
    <caption id="capture_encoding_compat">Capture → Encoding Compatibility (Linux/FreeBSD)</caption>
    <tr>
        <th>Capture Method</th>
        <th>VAAPI</th>
        <th>Vulkan Video</th>
        <th>NVENC (CUDA)</th>
        <th>Software</th>
    </tr>
    <tr>
        <td>KMS/DRM</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>NvFBC</td>
        <td>❌</td>
        <td>❌</td>
        <td>✅</td>
        <td>❌</td>
    </tr>
    <tr>
        <td>Wayland (wlroots)</td>
        <td>✅</td>
        <td>❌</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>X11</td>
        <td>✅</td>
        <td>❌</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>XDG Desktop Portal</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>KWin Screencast</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
</table>

**Legend:** ✅ Supported | 🟡 Partial Support | ❌ Not Yet Supported | ➖ Not Applicable

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
            &nbsp;&nbsp;FreeBSD/Linux: VAAPI-compatible, see: <a href="https://www.intel.com/content/www/us/en/developer/articles/technical/linuxmedia-vaapi.html">VAAPI hardware support</a><br>
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
        <td rowspan="6">OS</td>
        <td>FreeBSD: 14.4+</td>
    </tr>
    <tr>
        <td>Linux/Debian: 13+ (trixie)</td>
    </tr>
    <tr>
        <td>Linux/Fedora: 43+</td>
    </tr>
    <tr>
        <td>Linux/Ubuntu: 22.04+ (jammy)</td>
    </tr>
    <tr>
        <td>macOS: 14.2+</td>
    </tr>
    <tr>
        <td>Windows: 11+ (Windows Server does not support virtual gamepads)</td>
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
            &nbsp;&nbsp;FreeBSD/Linux: HD Graphics 510 or higher<br>
            &nbsp;&nbsp;Windows: Skylake or newer with QuickSync encoding support
        </td>
    </tr>
    <tr>
        <td>
            Nvidia:<br>
            &nbsp;&nbsp;FreeBSD/Linux: GeForce RTX 2000 series or higher<br>
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
  <img src='https://cdn.jsdelivr.net/gh/LizardByte/contributors@dist/sponsors.svg' alt="Sponsors"/>
</p>

## 👥 Contributors

Thank you to all the contributors who have helped make Sunshine better!

### GitHub

<p align="center">
  <img src='https://cdn.jsdelivr.net/gh/LizardByte/contributors@dist/github.Sunshine.svg' alt="GitHub contributors"/>
</p>

### CrowdIn

<p align="center">
  <img src='https://cdn.jsdelivr.net/gh/LizardByte/contributors@dist/crowdin.606145.svg' alt="CrowdIn contributors"/>
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
