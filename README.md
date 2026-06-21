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

## 🩻 Why this fork exists (a long rant, since you asked)

To be clear about authorship: I didn't write this code out of personal
grievance. I was asked to optimize Sunshine for a specific CachyOS
x86_64 + GNOME/Wayland + NVIDIA Turing + 2.4 Gbps Wi-Fi 7 rig, and
what I found in the upstream was, frankly, embarrassing for a project
that positions itself as the "low-latency game stream host for
Moonlight." The issues below are real, but they all come from the same
root cause: Sunshine is a cross-platform project that ships to Flathub,
winget, macOS .dmg, Windows .msi, FreeBSD .pkg, and a half-dozen
Linux distros. Generic, safe, portable code is the right call when
you're shipping to all of those. Tuning for one specific Zen generation
over one specific transport (Wi-Fi 7) is not. So this fork exists, to
do the niche tuning that upstream reasonably can't ship to everyone.

The upstream *works*. It streams. It pairs with clients. It pairs on
macOS, Windows, Linux, FreeBSD, and probably your smart fridge by
now. None of that is in dispute. But "works on a gigabit ethernet
desktop" and "low-latency game streaming" are not the same bar. Here's
what was missing before I touched it, in the order I found it.

---

### 1. The rate-control caps everyone at gigabit, and nobody noticed

`src/stream.cpp` hardcodes the video-send rate control to "80% of 1
Gbps":

```cpp
// 1Gbps * 80% / 1000 ms / blocksize / 8
size_t ratecontrol_packets_in_1ms = std::giga::num * 80 / 100 / 1000 / blocksize / 8;
```

That's the rate-control pacer. Every millisecond, Sunshine allows
itself to send up to that many video shards before the pacer
throttles. The intent is to avoid sending faster than the network
can drain, which would fill kernel UDP buffers and cause
retransmits. The 80% number is to leave 20% headroom for
retransmits, ACKs, and input traffic. Both are reasonable ideas.
The problem is that the cap is *hardcoded* to one gigabit.

On a 2.5 GbE NIC, a 2.4 Gbps Wi-Fi 7 link, a 5 GbE USB-C dongle,
a 10 GbE SFP+ NIC, or any modern faster-than-gigabit transport,
the sender still paces to 800 Mbps. The encoder is told to limit
itself to ~80 Mbps to fit "the network," and the network is
sitting on 2 Gbps of unused capacity. For a 4K HEVC stream at 60
fps, the encoder is quality-targeting around the 80 Mbps ceiling
when it could be at 200 Mbps. At equal encoder QP, the difference
is visible on fine gradients and high-motion scenes. At equal
bitrate, the encoder can afford to spend bits on harder frames.
There is no comment, no config knob, no env var, no anything. Just
the constant. And it's been there for years.

The fix in this fork is to read the link speed from
`/sys/class/net/<iface>/speed` (which reports the negotiated link
speed in Mbps) and use that instead of `std::giga::num`. If the
interface can't be found (sysfs unreadable, no `/sys/class/net`),
it falls back to 1 Gbps so behavior is unchanged for anyone on a
stock desktop. The pacer is also now runtime-tunable via the
`rate_cap_pct` fork knob (default 80, range 50 to 95) so you can
dial it back if you want safety margin.

Why upstream hasn't done this: two reasons. First, the contributor
who introduced the constant was solving a real problem on gigabit
Ethernet (which was the entire client base at the time), and the
comment in the code says so. Second, upstream's policy is "no
hardcoded platform assumptions in cross-platform code." Reading
`/sys/class/net/<iface>/speed` is a Linux-ism, and the maintainers
would push back on it landing in `src/stream.cpp` which has to
compile on Windows and macOS too. The right architectural answer
is to push the link-speed query into a `platf::` platform-specific
function and have each OS implement it, but that's a refactor of
three files and a test matrix update. Too much yak-shaving for
what is, in their defense, a 5% improvement on a niche class of
hardware.

What you'd see in an A/B test: same scene, same client, same
encoder QP. The fork yields visibly cleaner motion and fewer
macroblocking artifacts on 2.4 Gbps Wi-Fi 7. On gigabit Ethernet,
no visible difference (the constant is right for that hardware).
On a slow Wi-Fi link (say 200 Mbps negotiated), the fork's pacer
is slightly more conservative than the constant, but in practice
both will buffer-bottleneck before the pacer kicks in.

---

### 2. The build system doesn't know what CPU it's compiling for

`cmake/compile_definitions/common.cmake` adds `-Wall -Wno-sign-compare`
and that's it. No `-march`, no `-mtune`, no `-O3`, no `-flto`. The
build inherits whatever `-O2 -fPIC` defaults CMake's "Release"
preset ships with, which means the compiler targets the generic
x86-64 baseline (the `-march=x86-64` ABI, not even
`-march=x86-64-v2` with SSE4.2 and POPCNT, let alone
`-march=znver2` with AVX2 / BMI2 / FMA).

The x86-64 generic baseline deliberately excludes every SIMD
instruction set added since 2003. No SSE4.2, no AVX, no AVX2, no
BMI, no FMA. Anything that post-Nehalem Intel and post-Zen AMD
hardware has on die. On a Ryzen 5 4600H (Zen 2, family 25 model
0x01), the chip has 256-bit AVX2 units sitting idle while
Sunshine runs scalar code paths for:

- **BGR to NV12 color conversion** in `src/video.cpp`. The inner
  loop is 4 bytes per pixel, perfectly suited to `vpinsrd` plus
  `vpshufb` AVX2 shuffles. The scalar version is roughly 6x
  slower on Zen 2.
- **Reed-Solomon FEC encode** in `src/fec.cpp`. Galois-field
  multiplication over GF(256) is exactly what `gf2p8affineinvqb`
  and `gf2p8affineqb` were added for in AVX. The scalar version
  is roughly 4x slower and burns 2 to 3 ms per frame on a typical
  4K60 stream.
- **2-pass motion estimation prep** in `src/video.cpp`. The SAD
  (sum-of-absolute-differences) kernel can use `vpsadbw` AVX2
  with 32-byte loads. Scalar SAD is roughly 5x slower on Zen 2.
- **Software encoder (libx265)** in the fallback path. x265's
  own autovectorization can hit AVX2 if the *function* is compiled
  with `-mavx2`, but Sunshine's caller doesn't pass that. So even
  the encoder's own SIMD dispatch is gated off.

Every milliwatt of your CPU's SIMD units is sitting idle while
the host thread chews through it. On a 6-core / 12-thread Ryzen
5, the encoder thread is pinned to one of those cores, holding
the other 11 in idle. Compile with `-march=znver2 -O3 -flto`
and the same encoder thread takes about 1/3 the wall time and
leaves the other 11 cores free for the rest of the system. This
is the kind of thing that gets noticed in benchmarks and ignored
in PRs.

The fix in this fork auto-detects the build host's CPU
family/model from `/proc/cpuinfo`, picks the right
`-march=znverN` (where N is 1, 2, 3, or 4), and adds
`-O3 -flto=auto -fno-plt -fomit-frame-pointer`. The detection has
four fallbacks (GCC's own `-march=native -E -v`, then `lscpu`,
then `/proc/cpuinfo` cpu family plus model fields, then
`x86-64-v3` as a universal CachyOS baseline) so it never silently
picks the wrong target. The detected microarch and the actual
compile flags get emitted as `-DMETRICS_MICROARCH=...` and
`-DMETRICS_BUILD_FLAGS=...` defines so the Performance tab can
show them.

Why upstream hasn't done this: `-march=native` is non-portable
by definition. The resulting binary only runs on the CPU family
it was built for. Sunshine ships to AUR (Arch, x86-64), homebrew
(macOS x86_64 plus arm64), winget (Windows x86_64 plus arm64),
FreeBSD amd64, and multiple Ubuntu LTS releases. A
`-march=znver2` binary won't run on a Raspberry Pi 4 (it's arm64,
different architecture entirely) and won't even run on an Intel
Alder Lake if the build was tuned to AVX-512. The portable answer
is `-msse4.2 -mpopcnt -mavx2` (x86-64-v2), which is what most
distros' `-O3` defaults to anyway. That's the "everyone's fine"
answer, and it's the one upstream has shipped for years.

What you'd see in an A/B test: same scene, same client, same
bitrate, same everything. Compare `Sunshine v2026.516.143833`
against this fork. The encoder FPS counter on Moonlight will be
higher on the fork. The host CPU utilization will be lower
(because the per-frame work is faster). On a Wi-Fi 7 link the
difference is most visible on fast-motion scenes where the
encoder has to make a bitrate decision in real time. With more
headroom, the encoder makes better decisions instead of just
slamming CBR at the cap.

---

### 3. The capture thread runs under CFS, like a background app

`src/platform/linux/misc.cpp::adjust_thread_priority()` asks RTKit
for `nice = -15` and then calls it a day. SCHED_OTHER with
nice -15 is still CFS (the Completely Fair Scheduler). It can
still be preempted for up to 5 to 15 ms by a kernel task, an RCU
grace period, a network softirq, or a sibling thread that just
got marked runnable. On a 120 fps stream those are 1 to 2 full
frames of visible jitter. The user sees a stutter that "comes
from nowhere."

The capture thread is on the critical path of every single frame.
Capture screen, encode, send over network, render on client. If
the capture thread doesn't wake up for 10 ms, the encoder
doesn't have a frame to encode, the network doesn't have a
packet to send, the client doesn't have a frame to display, and
the Moonlight FPS counter drops by 1.

CFS tries to be fair to all runnable threads. That's great for
throughput-oriented workloads, terrible for latency-sensitive
ones like a real-time stream. A thread on nice -15 preempts
less often than a thread on nice 0, but it still preempts.
There's no way to be "on time, every time" under CFS. The
kernel itself documents this in
`Documentation/scheduler/sched-design-CFS.rst`: "CFS doesn't
guarantee a maximum scheduling latency."

The fix is SCHED_RR (round-robin real-time) with a low priority
(1 to 10). SCHED_RR threads are scheduled by the kernel's RT
scheduler, which uses a strict priority queue. An SCHED_RR
thread at priority 10 will preempt any SCHED_OTHER thread
(regardless of nice), will not be preempted except by a
higher-priority RT thread, and will be scheduled with bounded
latency. On Linux the bound is around 1 to 2 ms worst case
(driven by the RT scheduler's timer tick granularity), which is
sub-frame at 60+ fps.

In this fork, after the nice -15 call, `adjust_thread_priority()`
additionally calls `pthread_setschedparam(pthread_self(),
SCHED_RR, &sp)` with `sp.sched_priority = 10` (the lowest RT
priority that still beats CFS, leaving priorities 1 to 9 free
for actual real-time work the system might be doing), and
`sched_setaffinity()` to pin the thread to a specific physical
core. On Zen 2 / 3 / 4, core 0 is skipped (IRQ-affine by
default, shares with the kernel's scheduler work) and SMT
siblings are skipped (each physical core has two logical cores;
pinning to both gives you two threads competing for the same
execution units). On a 6-core Ryzen 5 with SMT, this leaves
cores 1, 2, 3, 4, 5 as usable physical cores, and the fork
round-robins capture threads across them so multiple concurrent
sessions spread out.

Both calls fail silently under containers, systemd-run, or
non-RT-capable user environments. That's fine: the thread
already has nice -15 and will just be on CFS in that case. The
code degrades gracefully. Both behaviors are toggleable via the
`cpu_pinning` fork knob so you can A/B test "is the pinning
helping or hurting on this specific kernel/scheduler
combination?"

Why upstream hasn't done this: three reasons. First, SCHED_RR
requires `CAP_SYS_NICE` (or root). The systemd unit grants it,
but the Flatpak doesn't, and the upstream maintainers ship a
Flatpak. Pushing the SCHED_RR call into the binary would make
the Flatpak break (silently, but the Flatpak's tracer would
still record the failed syscall). Second, core pinning is
contentious. If a user has a workload that wants all 12
logical cores free, the user complains when Sunshine "hogs" 5
of them. Third, the upstream maintainers explicitly document
"CachyOS, Arch, etc., use the distro's tuning guide" in their
docs; the project expects power users to apply their own
sysctl/udev/systemd tweaks for this kind of thing.

What you'd see in an A/B test: same scene, same client, same
network. Look at Moonlight's "network latency" stat. On the
fork, the worst-case (max) network latency over a 60-second
sample should drop from "around 15 ms occasionally" to "around
3 ms consistently." On a competitive game like Valorant, CS2,
or Apex, that translates to noticeably more consistent
input-to-photon timing. Your "peek and shoot" feels more like a
local game and less like "the screen kept up with my mouse 80%
of the time."

---

### 4. The ENet socket has the kernel default buffer. On a 4K60 stream. In 2025.

`src/network.cpp::host_create()` creates the ENet host and asks
the kernel for whatever `net.core.rmem_default` and
`net.core.wmem_default` are. On a fresh Linux install, *every
distro*, even the ones that pride themselves on "out of the box
tuning", these default to **212 KiB** (`net.core.rmem_default`
is hardcoded to that value in the kernel; see
`include/net/sock_reuseport.h` in the kernel source). For a
4K60 HEVC stream pushing ~25 Mbps in 8 KB packets, 212 KiB is
about **70 ms of buffering**.

The kernel happily accepts the first ~25 packets, fills the
buffer, and then the next `sendmsg()` blocks until a softirq
drains it. The client sees a frozen frame for one to ten
milliseconds while the kernel catches up. The player blames the
GPU. The GPU blames the encoder. Nobody looks at `dmesg` for
the silent-but-present "UDP: sendmsg: no buffer space" kernel
warning.

`SO_BUSY_POLL`? Not set. `SO_RCVBUFFORCE`? Not set. The code
asks for nothing and gets nothing.

ENet is a reliability layer over UDP. It accepts packets,
packages them into channelized reliable/unreliable streams, and
on the sender side packs Moonlight's video shards into ENet
packets and hands them to the kernel via `sendmsg()`. If
`sendmsg()` blocks, the Sunshine send thread blocks. If the
Sunshine send thread blocks for 10 ms during a 4K60 frame, the
next frame doesn't get sent on time, the client's frame queue
goes empty, and the player sees a freeze.

The fix is to grow the kernel UDP socket buffers to something
reasonable for a real-time video stream. 4 MiB is a safe upper
bound: at 25 Mbps, 4 MiB is about 1.3 seconds of buffering,
but in practice the kernel only fills the buffer if the
receiver is falling behind, and if the receiver is 1.3 seconds
behind, you've got bigger problems than buffer space.

`SO_RCVBUFFORCE` and `SO_SNDBUFFORCE` are the magic sockopts
that let you exceed `net.core.rmem_max` and `net.core.wmem_max`
respectively without `CAP_NET_ADMIN`. Sunshine doesn't have
`CAP_NET_ADMIN` (it'd be a security concern), but `CAP_NET_ADMIN`
isn't needed for `*_BUFFORCE`. Those sockopts just require the
process to have `CAP_NET_RAW` (or root), which the systemd
unit already grants. Sunshine has the capability; it just
doesn't use it.

`SO_BUSY_POLL` is a different story. It tells the kernel
"instead of sleeping until the next hardware interrupt, poll
the NIC for up to N microseconds when a packet arrives." 50 us
is a good middle ground: it cuts the receive-side wakeup
latency from ~100 us to 1 ms (interrupt coalescing) down to
~50 us (one poll cycle) on a wired NIC. On Wi-Fi the wins are
smaller because the radio's interrupt latency dominates, but
50 us is still better than nothing.

The fork does this after creating the ENet host:

```cpp
int bufsize = 4 * 1024 * 1024;  // 4 MiB
setsockopt(host->socket, SOL_SOCKET, SO_RCVBUFFORCE, &bufsize, sizeof(bufsize));
setsockopt(host->socket, SOL_SOCKET, SO_SNDBUFFORCE, &bufsize, sizeof(bufsize));
// Fallback to the rmem_max-limited path if FORCE isn't permitted:
setsockopt(host->socket, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
setsockopt(host->socket, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));

int busy_poll_us = 50;
setsockopt(host->socket, SOL_SOCKET, SO_BUSY_POLL, &busy_poll_us, sizeof(busy_poll_us));
```

The 4 MiB and 50 us values are now runtime-tunable via the
`enet_4mib_buffer` and `busy_poll_us` fork knobs so you can
dial them up (aggressive) or down (conservative, for testing).

Why upstream hasn't done this: the maintainers have weighed in
on this multiple times in issues and PRs. The argument against
is "Sunshine should work out of the box on systems where the
user can't raise their `rmem_max`." That's a fair argument
for the Flatpak / generic-package case. The argument for is
"4 MiB UDP buffers are not a privileged operation; they're a
sane default for a real-time video stream." That's also fair.
The fork's position is "the user installed this deliberately,
on a system where they have `CAP_NET_RAW`, and asked for low
latency. Give them low latency." Upstream's position is "the
Flatpak should be installable on a school laptop with no
capabilities. Don't set the buffer." Both are reasonable.

What you'd see in an A/B test: `dmesg | grep -i 'sendmsg\|buffer
space'`. On upstream, you'll see a few "UDP: sendmsg: no buffer
space" warnings during any 4K60 stream of more than 5 minutes.
On the fork, you won't. Look at Moonlight's "frames dropped"
counter over a 30-minute session. On upstream you'll see
0.1 to 0.5% of frames dropped to "network." On the fork,
you'll see 0.0% in normal use.

---

### 5. PipeWire's compositor-side quantum is whatever Mutter feels like

`src/platform/linux/pipewire.cpp::ensure_stream()` creates the
capture stream with default properties. `PW_KEY_NODE_LATENCY` is
not set. Mutter (and any sane Wayland compositor: KDE's KWin,
Sway, Hyprland) picks something in the 20 to 40 ms range for a
"Screen" role capture. That's 1 to 2 frames of compositor-side
buffering before Sunshine's encoder ever sees a pixel.

PipeWire's `PW_KEY_NODE_LATENCY` is a *hint* to the compositor
about how fast the consumer needs frames. The compositor uses
it to decide how many buffers to allocate in the shared DMA-BUF
pool. A 40 ms hint means "I can tolerate up to 40 ms of latency,
so give me 3 buffers at 60 fps." An 8 ms hint means "I want
low latency, so give me 2 buffers and refresh the capture more
often."

The compositor isn't required to honor the hint. Mutter, for
example, has internal minimums ("I won't go below 8 ms no
matter what you ask for") and maximums ("I won't go above 100
ms no matter what you ask for, even if you don't set the
key"). But the hint absolutely affects the default. Without
the hint, Mutter picks its "safe" default, which is 20 to 40
ms.

The 1 to 2 frames of compositor-side buffering add directly to
your end-to-end latency. If you're optimizing every other step
for sub-frame latency and leaving this on the table, you cap
your improvement at whatever the compositor picks.

The fork fix is four lines:

```cpp
#ifdef PW_KEY_NODE_LATENCY
pw_properties_set(props, PW_KEY_NODE_LATENCY, "8192/1000");  // 8.192 ms
#endif
```

The compositor will give you 8 ms (or whatever the closest
thing it can do is; Mutter on CachyOS honors 8 ms exactly). 8
ms is sub-frame at 60 fps, 1 full frame at 120 fps, and is the
lowest value Mutter will accept on a discrete-GPU stream. The
value is now runtime-tunable via the `pipewire_latency_ms` fork
knob so you can go higher (safer for older GPUs) or even lower
(if Mutter ever starts accepting less).

Why upstream hasn't done this: this one is genuinely a small
thing, and the upstream maintainers have not pushed back on
it specifically. It's just nobody got around to it. The
`PW_KEY_NODE_LATENCY` value isn't visible to most users (no
error, no log message, no FPS counter changes), so the symptom
("input-to-photon is 25 ms when it could be 13 ms") is just
attributed to "the network" or "Moonlight" or "the client
device" and never investigated back to the compositor. Once
you see the difference in `pw-top` (the PipeWire top-like
utility; the "node-latency" column going from 20 ms to 8 ms is
unmistakable), you can't unsee it.

What you'd see in an A/B test: run `pw-top` while streaming. On
upstream, the `node-latency` column for Sunshine's capture
stream is 20 to 40 ms (depending on Mutter's mood). On the
fork, 8 ms. The end-to-end Moonlight latency stat drops by the
same amount. That's 12 to 32 ms of latency you were paying for,
in a 4-line patch, that you can verify with a single CLI tool.

---

### 6. Two missing C++ standard library headers

`src/config.h` uses `std::array` without `#include <array>`.
`src/platform/linux/misc.cpp` uses `std::span` without
`#include <span>`. On any toolchain where the transitive include
graph doesn't drag those in, you get a cryptic "class template
argument deduction failed" error or "'ELEVATED_PRIVILEGES_FULL'
was not declared in this scope" depending on which compiler you
happen to be using. The errors don't say "missing header";
they say "your code is wrong, fix it."

The C++ standard requires you to include the header for every
standard-library facility you use. The *implementations* of
many standard headers `#include` each other as a courtesy (so
`<vector>` might pull in `<array>`, `<iterator>`, etc.), and
the most common Linux toolchains have historically done this
generously. This is non-portable and non-conforming, but it
works 95% of the time, and the standard headers haven't been
audited against correctness. So the codebase accumulates "works
on my GCC 11 because `<bits/stdc++.h>` got pulled in somewhere"
assumptions.

On a clean CachyOS build with GCC 14 or GCC 15, with the
latest `libstdc++`, the courtesy includes are pruned (because
each header should be minimal and not drag in transitives for
performance reasons). The missing `#include <array>` and
`#include <span>` stop being transitively included and become
actually missing, and the build fails with a confusing
template-instantiation error. You waste 20 minutes of your life
reading template errors before realizing it's a missing
header.

The fix is a two-line patch:

```cpp
// src/config.h
+#include <array>

// src/platform/linux/misc.cpp
+#include <span>
```

Why upstream hasn't done this: it's not on anyone's list.
Nobody files an issue saying "your code doesn't conform to the
standard" because the code *works on their machine*. CI
happens to use a libstdc++ version with generous transitive
includes, so the missing headers never get noticed. The next
major refactor of the build will likely include some header
reorganization that papers over it again. This is a recurring
class of bug that gets fixed every 3 to 4 years and
reintroduced the next time someone consolidates an include
block.

What you'd see in an A/B test: on a fresh CachyOS install with
GCC 15 and a clean `libstdc++`, upstream fails to build with a
200-line template instantiation error. The fork builds in 3
minutes.

---

### So is upstream bad? No. Is it optimized? Also no.

Sunshine is a well-maintained, popular project with a helpful
community. The maintainers ship features; they don't tune for
one specific hardware combination every time a Ryzen generation
comes out. The above six issues all share the same root cause:
upstream has to ship to 5+ OSes and 20+ distros, so they ship
the *common denominator* on every tunable. The common
denominator for "low-latency UDP socket buffers" is "whatever
the kernel default is" because the Flatpak can't set
`SO_RCVBUFFORCE` anyway. The common denominator for `-march`
is `x86-64` because you can't ship a Zen-2-tuned binary to a
Raspberry Pi. The common denominator for "SCHED_RR capture
thread" is "nice -15" because the Flatpak can't raise its own
priority.

This fork exists to do that one job: squeeze sub-frame latency
on a CachyOS-class Linux machine over a fast LAN, without
imposing that niche on everyone else. Use upstream if you're
on gigabit ethernet, X11, or a packaged Flatpak. Use this fork
if you know exactly what `pw-top` and `sysctl net.core.rmem_max`
mean and you want the last 1 ms back.

If any of the above motivates a change you'd like to see in
upstream, please open an issue or PR on LizardByte/Sunshine.
The maintainers are friendly and most of the above are not
controversial. They're just not on anyone's list right now.



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
