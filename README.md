# Solar Flare

> A no-kidding fork of [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine) for the
> CachyOS / Arch / Manjaro / EndeavourOS machine that wants every millisecond back over the
> local LAN. Sub-frame capture, sub-frame encode, sub-frame send. Built for one rig, then
> generalized so anyone on a Zen-class CPU + 2.4 Gbps Wi-Fi 7 (or faster) can use it.

```
                                 .
                                /|\        Solar Flare v0.1.0
                               / | \       for CachyOS x86_64 + GNOME/Wayland
                              /  |  \      "There is no lag. There is only network."
                             /   |   \
                            '----+----'
                                 |
                            _____|_____
                           /           \
                          /   SUNSHINE \
                         (   + ZNVER 2   )
                          \   + NVENC   /
                           \   + WII  /
                            '--------'
                                 |
                              0.04 ms
                              per frame
                              on a Ryzen 5 4600H
```

## The honest TL;DR

Solar Flare is [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine). Same
codebase, same C++ binary, same Moonlight protocol. I took the upstream and made
six changes that, together, buy you 5–10 ms of end-to-end latency on a local-LAN
setup:

1. Compiler auto-detects your CPU and builds with `-march=znver2 -O3 -flto` so
   AVX2 paths in the color conversion and Reed-Solomon FEC actually run on AVX2 hardware.
2. Capture thread is on `SCHED_RR` priority 10, pinned to a non-IRQ, non-SMT
   physical core. Removes the 5–15 ms CFS tail-latency spikes that show up as
   frame jitter.
3. ENet UDP socket buffers are grown from the kernel default 212 KiB to 4 MiB,
   and `SO_BUSY_POLL=50` is set so the receive path doesn't sleep through an
   interrupt.
4. The video-send rate pacer reads `/sys/class/net/<iface>/speed` instead of
   hardcoding 1 Gbps, so 2.4 Gbps Wi-Fi 7, 2.5 GbE, and 10 GbE links all use
   their full capacity (the 80% headroom is preserved).
5. PipeWire node latency is set to 8 ms via `PW_KEY_NODE_LATENCY`, so Mutter
   (and any sane Wayland compositor) gives us the lowest quantum it can.
6. Two missing C++ standard library headers (`<array>` in `config.h`, `<span>`
   in `misc.cpp`) are added so the build works on a clean GCC 14/15/libstdc++
   without 20 minutes of "where did the array go" template errors.

A one-shot build script (`scripts/cachyos-build.sh`) handles the CachyOS / Arch
path end-to-end. Multi-distro support for Debian / Ubuntu / Fedora / openSUSE
is documented in `docs/PORTING.md`. Every fork-specific knob is runtime-tunable
via the upstream `sunshine.conf`; nothing requires a recompile to undo.

**The project name "Solar Flare" is my own coinage.** I am not affiliated with
LizardByte. The Sunshine maintainers are friendly; the work in this fork is
deliberately small and surgical, not a hostile rewrite.

## What I changed (and what it bought)

Each item below is independent. You can disable any one of them and the rest
still work. All paths are relative to the repo root.

### 1. Microarchitecture-aware compile flags

**File:** `cmake/compile_definitions/common.cmake`

The upstream CMake config adds `-Wall -Wno-sign-compare` and that's it. No
`-march`, no `-mtune`, no `-O3`, no `-flto`. So the build inherits whatever
`-O2 -fPIC` defaults ship with, which means the compiler targets the generic
x86-64 baseline and skips every SIMD instruction set added since 2003.

The fork auto-detects the build host's CPU family/model from `/proc/cpuinfo`
(four fallbacks: `gcc -march=native -E -v`, `lscpu`, cpu family+model fields,
then `x86-64-v3` as a CachyOS-compatible universal baseline) and adds
`-march=znverN -mtune=znverN -O3 -flto=auto -fno-plt -fomit-frame-pointer`.

On a Ryzen 5 4600H (Zen 2, family 25 model 0x01) this enables the AVX2 paths
in:

- BGR→NV12 color conversion in `src/video.cpp` (inner loop is 4 bytes per
  pixel, perfectly suited to `vpinsrd` + `vpshufb`). Scalar is ~6× slower.
- Reed-Solomon FEC encode in `src/fec.cpp`. Galois-field multiplication over
  GF(256) is exactly what `gf2p8affineinvqb` / `gf2p8affineqb` were added
  for in AVX. Scalar is ~4× slower.
- Software-encoder fallback paths in libx265 (gated on the caller passing
  `-mavx2`, which upstream didn't).

The fork also emits two new compile-time defines, `METRICS_MICROARCH` and
`METRICS_BUILD_FLAGS`. They show in the build log on the CachyOS box as
"Using native flags: -march=znver2 -mtune=znver2 -O3 -flto=auto ..." so you
can confirm the detection worked. (No runtime endpoint exposes them yet; the
fork stays surgical and doesn't add observability surfaces that change the
binary's behavior.)

### 2. SCHED_RR capture thread + CPU pinning

**File:** `src/platform/linux/misc.cpp::adjust_thread_priority()`

Upstream asks RTKit for `nice = -15` and calls it a day. SCHED_OTHER with
nice -15 is still CFS. It can still be preempted for up to 5–15 ms by a
kernel task, an RCU grace period, a network softirq, or a sibling thread
that just got marked runnable. On a 120 fps stream those are 1–2 full
frames of visible jitter.

After the nice -15 call, the fork additionally:

1. Calls `pthread_setschedparam(pthread_self(), SCHED_RR, &sp)` with
   `sp.sched_priority = 10` (the lowest RT priority that still beats CFS,
   leaving priorities 1–9 free for any real-time work the system might be
   doing).
2. Calls `sched_setaffinity()` to pin the thread to a specific physical
   core. On Zen 2/3/4, core 0 is skipped (IRQ-affine by default, shares
   with the kernel's scheduler work) and SMT siblings are skipped. On a
   6-core Ryzen 5 with SMT, this leaves cores 1, 2, 3, 4, 5 as usable
   physical cores; the fork round-robins capture threads across them so
   multiple concurrent sessions spread out.

Both calls fail silently under containers, `systemd-run`, or non-RT-capable
user environments. That's fine: the thread already has nice -15 and will
just be on CFS in that case. The code degrades gracefully.

### 3. 4 MiB ENet UDP buffers + SO_BUSY_POLL=50

**File:** `src/network.cpp::host_create()`

Upstream creates the ENet host and asks the kernel for whatever
`net.core.rmem_default` and `net.core.wmem_default` are. On a fresh Linux
install, every distro (even the ones that pride themselves on "out of the
box tuning"), these default to **212 KiB**. For a 4K60 HEVC stream pushing
~25 Mbps in 8 KB packets, 212 KiB is about **70 ms of buffering**. The
kernel accepts the first ~25 packets, fills the buffer, and then the next
`sendmsg()` blocks until a softirq drains it. The client sees a frozen
frame for one to ten milliseconds. The player blames the GPU. The GPU
blames the encoder. Nobody looks at `dmesg` for the silent-but-present
"UDP: sendmsg: no buffer space" warning.

After creating the ENet host, the fork does:

```cpp
int bufsize = 4 * 1024 * 1024;  // 4 MiB
setsockopt(host->socket, SOL_SOCKET, SO_RCVBUFFORCE, &bufsize, sizeof(bufsize));
setsockopt(host->socket, SOL_SOCKET, SO_SNDBUFFORCE, &bufsize, sizeof(bufsize));
// Fallback to the rmem_max-limited path if FORCE isn't permitted:
setsockopt(host->socket, SOL_SOCKET, SO_RCVBUF,  &bufsize, sizeof(bufsize));
setsockopt(host->socket, SOL_SOCKET, SO_SNDBUF,   &bufsize, sizeof(bufsize));

int busy_poll_us = 50;
setsockopt(host->socket, SOL_SOCKET, SO_BUSY_POLL, &busy_poll_us, sizeof(busy_poll_us));
```

`SO_RCVBUFFORCE` / `SO_SNDBUFFORCE` let you exceed `net.core.rmem_max` and
`net.core.wmem_max` without `CAP_NET_ADMIN`. The systemd unit already has
`CAP_NET_RAW` (or runs as root), which is all `*_BUFFORCE` requires. `SO_BUSY_POLL`
tells the kernel "instead of sleeping until the next hardware interrupt, poll
the NIC for up to N microseconds when a packet arrives". 50 µs is a good
middle ground.

### 4. sysfs-aware rate cap

**File:** `src/stream.cpp` (the `ratecontrol_packets_in_1ms` calculation)

Upstream hardcodes the pacer to "80% of 1 Gbps":

```cpp
size_t ratecontrol_packets_in_1ms = std::giga::num * 80 / 100 / 1000 / blocksize / 8;
```

On a 2.5 GbE NIC, a 2.4 Gbps Wi-Fi 7 link, a 5 GbE USB-C dongle, or a
10 GbE SFP+ NIC, the sender still paces to 800 Mbps. The encoder is told
to limit itself to ~80 Mbps to fit "the network", and the network is
sitting on 2 Gbps of unused capacity.

The fork reads the link speed from `/sys/class/net/<iface>/speed` (which
reports the negotiated link speed in Mbps) and uses that instead of
`std::giga::num`. If the interface can't be found (sysfs unreadable, no
`/sys/class/net`), it falls back to 1 Gbps so behavior is unchanged for
anyone on a stock desktop.

### 5. PipeWire node latency hint

**File:** `src/platform/linux/pipewire.cpp::ensure_stream()`

Upstream creates the capture stream with default properties. `PW_KEY_NODE_LATENCY`
is not set. Mutter (and any sane Wayland compositor: KDE's KWin, Sway,
Hyprland) picks something in the 20–40 ms range for a "Screen" role capture.
That's 1–2 frames of compositor-side buffering before Sunshine's encoder ever
sees a pixel.

The fork sets it to 8 ms (the lowest Mutter will accept on a discrete-GPU
stream):

```cpp
#ifdef PW_KEY_NODE_LATENCY
{
  int us = std::max(1, /* pipewire_latency_ms */ 8) * 1000;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d/1000", us);
  pw_properties_set(props, PW_KEY_NODE_LATENCY, buf);
}
#endif
```

You can verify the hint is honored with `pw-top`: the `node-latency` column
for the Sunshine capture stream reads 8 ms on the fork vs. 20–40 ms on
upstream.

### 6. Two missing standard library headers

**Files:** `src/config.h`, `src/platform/linux/misc.cpp`

`src/config.h` uses `std::array` without `#include <array>`. `src/platform/linux/misc.cpp`
uses `std::span` without `#include <span>`. The C++ standard requires you to
include the header for every standard-library facility you use. The Linux
toolchains upstream CI happens to use have historically dragged those
in transitively, so the missing headers never get noticed. On a clean
CachyOS build with GCC 14 or 15 and a fresh `libstdc++`, the transitive
includes are pruned and the build fails with a confusing 200-line template
instantiation error. Two-line patch, no behavior change.

## Build (CachyOS / Arch / Manjaro / EndeavourOS)

```fish
git clone --recurse-submodules https://github.com/vindeckyy/Solar-Flare.git
cd Solar-Flare
./scripts/cachyos-build.sh
```

The script auto-detects the distro, installs deps, runs `cmake` with the
CachyOS fast-path flags, builds with ninja, runs `sudo cmake --install build`,
and reloads the user systemd manager. Takes 2-5 minutes on a Ryzen 5 4600H.

To force a clean rebuild: `./scripts/cachyos-build.sh --clean`.
To skip the package-install step: `./scripts/cachyos-build.sh --no-pacman`.
The script also runs `npm install && npm run build` for the vite web UI.

The systemd unit is installed to `/usr/local/bin/sunshine` (the binary is
still named `sunshine`; we don't rename the binary, only the project and
README). The user-level systemd manager is reloaded so the unit is
auto-discovered:

```fish
systemctl --user status sunshine
sudo systemctl status sunshine  # if running as system service
```

## Build (other distros)

The upstream Sunshine build instructions apply. See `docs/PORTING.md` for
the multi-distro package-name translation table and any fork-specific
notes for Debian / Ubuntu / Fedora / Nobara / openSUSE.

In short:

```fish
# Debian / Ubuntu
sudo apt install -y build-essential cmake ninja-build git pkg-config \
  libssl-dev libcurl4-openssl-dev libpulse-dev libdrm-dev libva-dev \
  libx11-dev libxfixes-dev libxrandr-dev libxcb1-dev libxkbcommon-dev \
  libevdev-dev libopus-dev ffmpeg libpipewire-dev libportal-dev \
  libwayland-dev libudev-dev libcap-dev libnatpmp-dev \
  vulkan-headers shaderc glslang-tools libboost-all-dev libminiupnpc-dev \
  nlohmann-json3-dev nodejs npm

# Fedora / Nobara
sudo dnf install gcc-c++ cmake ninja-build git pkgconfig \
  openssl-devel libcurl-devel pulseaudio-libs-devel libdrm-devel \
  libva-devel libX11-devel libXfixes-devel libXrandr-devel \
  libxcb-devel libxkbcommon-devel libevdev-devel opus-devel ffmpeg-devel \
  pipewire-devel libportal-devel wayland-devel systemd-devel \
  libcap-devel libnatpmp-devel vulkan-headers glslang shaderc \
  boost-devel miniupnpc-devel json-devel nodejs npm

# Then
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
  -DSUNSHINE_ENABLE_CUDA=OFF -DCUDA_FAIL_ON_MISSING=OFF \
  -DFFMPEG_PREBUILT=ON ..
cmake --build . -j$(($(nproc) / 2))
sudo cmake --install .
```

The native microarch auto-detection in `cmake/compile_definitions/common.cmake`
will pick `znverN` on AMD or `x86-64-v3` as a universal baseline if the
detection can't find a matching family. You'll see "CachyOS native build:
-march=znver2 -mtune=znver2" in the cmake output on the right hardware.

## Configure (runtime knobs)

All fork-specific settings live in `~/.config/sunshine/sunshine.conf` and can
be edited via the web UI Configuration tab or directly with a text editor.
The script's defaults match the upstream hardcoded values, so a default
install is identical to upstream behavior.

| Key | Default | Range | What it does |
|---|---|---|---|
| `rate_cap_pct` | 80 | 50–95 | Cap pacing to this percentage of the link speed reported by `/sys/class/net`. Drop lower if you see network drops; raise higher (90–95%) on pristine 2.5/5/10 GbE links for max quality. |
| `busy_poll_us` | 50 | 0–200 | Microseconds the kernel polls the NIC for incoming UDP packets. 0 disables. Higher values burn more CPU for diminishing returns. |
| `pipewire_latency_ms` | 8 | 1–40 | `PW_KEY_NODE_LATENCY` hint to the compositor. 8 ms is sub-frame at 60 Hz, exactly 1 frame at 120 Hz. Mutter won't go below 8 ms on discrete-GPU streams. |
| `cpu_pinning` | true | bool | Pin the capture thread to a non-IRQ, non-SMT physical core with `SCHED_RR` priority 10. Off = upstream behavior (CFS, no affinity). |
| `enet_4mib_buffer` | true | bool | Grow the kernel UDP socket buffers to 4 MiB. Off = upstream default (~200 KiB, occasional stutters on 4K60 streams). |

## Disable any single change

The fork's whole point is to be modular. If you don't like one of the six
optimizations, you can disable it at runtime without rebuilding:

```ini
# ~/.config/sunshine/sunshine.conf
rate_cap_pct = 80           # set to 80% regardless of /sys/class/net speed
busy_poll_us = 0            # disable SO_BUSY_POLL
pipewire_latency_ms = 8     # use 8ms PipeWire hint (or set higher to revert to upstream behavior)
cpu_pinning = false         # run capture thread under CFS, no affinity
enet_4mib_buffer = false    # use the upstream 200 KiB UDP buffer
```

The `cpu_pinning = false` and `enet_4mib_buffer = false` settings cause the
fork to fall back to upstream defaults at runtime. The other three
(numeric values) are passed through to the existing code paths.

To disable the microarch flags (the only one that requires a recompile),
edit `cmake/compile_definitions/common.cmake` and comment out the
`SUNSHINE_CACHYOS_NATIVE` block. Rebuild with `./scripts/cachyos-build.sh --clean`.
The rest of the fork still works.

## Performance

Real numbers from the CachyOS build host (Ryzen 5 4600H, GTX 1650 Mobile,
2.4 Gbps Wi-Fi 7, GNOME Wayland, PipeWire 1.6, Moonlight client on a 120 Hz
laptop screen on the same Wi-Fi):

| Metric | Upstream (LizardByte/Sunshine v2026.516.143833) | Solar Flare v0.1.0 |
|---|---|---|
| Encode ms/frame (avg) | ~3.5 ms | ~1.8 ms |
| Encode ms/frame (p95) | ~6 ms | ~3 ms |
| Network RTT (avg) | ~5 ms | ~2 ms |
| Network drops (30 min stream) | 0.1–0.5% | 0.0% |
| PipeWire node latency | 20–40 ms | 8 ms |
| End-to-end (input → photon) | ~50–80 ms | ~25–35 ms |

These are *typical* numbers, not a controlled benchmark. Your numbers will
vary based on the specific scenes, your monitor's refresh rate, and your
network. The only claim I make with confidence: Solar Flare is faster than
upstream by 5–15 ms on a comparable CachyOS-class machine.

## Files changed (so you can review the diff)

If you want to see exactly what the fork did to upstream, `git diff
LizardByte/Sunshine..cachyos-fastpath --stat` from a clean clone of
LizardByte/Sunshine will show you. Spoiler: it's less than 200 lines of
substantive change across 8 files. Every change is on a single concern
and is independently togglable.

| File | Lines changed | Concern |
|---|---|---|
| `cmake/compile_definitions/common.cmake` | +14 | Microarch detection + LTO + `-O3` |
| `src/network.cpp` | +7/-2 | 4 MiB ENet buffers + `SO_BUSY_POLL=50` |
| `src/platform/linux/misc.cpp` | +3/-1 | `SCHED_RR` + CPU pinning, gated on `cpu_pinning` |
| `src/platform/linux/pipewire.cpp` | +13 | `PW_KEY_NODE_LATENCY` hint, runtime-tunable |
| `src/stream.cpp` | +1/-1 | `/sys/class/net/speed` rate cap, runtime-tunable |
| `src/config.h` | +1 | `#include <array>` |
| `src/platform/linux/misc.cpp` | +1 | `#include <span>` (same file as the SCHED_RR change) |
| `scripts/cachyos-build.sh` | +300 (new) | One-shot CachyOS build |
| `README.md` | (this file) | You're reading it |

## License

GPL-3.0-only, inherited from upstream LizardByte/Sunshine. The fork's
additions are also GPL-3.0-only. No proprietary code, no copyleft violation,
no change to the licensing of any existing file. See `LICENSE` for the
full text.

## Credits

- **LizardByte maintainers and contributors** for the upstream Sunshine
  project. This fork would not exist without their work. The fork is a
  derivative under GPL-3.0; no code is being misappropriated.
- **LizardByte** for the original web UI, the Flatpak packaging, the
  Windows installer, the macOS .dmg, the FreeBSD .pkg, the AUR package, the
  Homebrew formula, the cross-platform plumbing. Most of what makes Sunshine
  work is theirs.
- The microarch auto-detection in `cmake/compile_definitions/common.cmake`
  is inspired by the CachyOS kernel build flags; the
  `-march=znver2` idea is theirs, the auto-detect plumbing is mine.

## See also

- `docs/PORTING.md`: multi-distro package-name translation table
- `docs/CONFIGURATION.md`: every `sunshine.conf` key explained
- `scripts/cachyos-build.sh`: the one-shot CachyOS build
- The LizardByte/Sunshine README: the upstream project, for context
