# SolarFlare

> Fork of [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine) for Zen-class CPUs on local LAN.

```
                                 .
                                /|\        SolarFlare v0.1.0
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

A derivative of upstream LizardByte/Sunshine. Same codebase, same C++ binary,
same Moonlight protocol. Same binary name (`sunshine`), same service
(`sunshine.service`), same user config dir (`~/.config/sunshine/`).

`docs/PORTING.md` covers multi-distro package-name translation.

## Build (CachyOS / Arch / Manjaro / EndeavourOS)

```fish
git clone --recurse-submodules https://github.com/vindeckyy/Solar-Flare.git
cd Solar-Flare
./scripts/cachyos-build.sh
```

To force a clean rebuild: `./scripts/cachyos-build.sh --clean`.
To skip the package-install step: `./scripts/cachyos-build.sh --no-pacman`.
The script runs `npm install && npm run build` for the vite web UI before
calling cmake.

## Build (other distros)

See `docs/PORTING.md` for the package-name translation table for
Debian / Ubuntu / Fedora / Nobara / openSUSE.

Short form:

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

## Configure

All fork-specific settings live in `~/.config/sunshine/sunshine.conf`. Edit
via the web UI Configuration tab or directly. Defaults match upstream
hardcoded values, so a default install behaves identically to upstream.

| Key | Default | Range | Effect |
|---|---|---|---|
| `rate_cap_pct` | 80 | 50–95 | Percent of `/sys/class/net/<iface>/speed` to use as the rate-control pacer. |
| `busy_poll_us` | 50 | 0–200 | `SO_BUSY_POLL` on the ENet socket. 0 disables. |
| `pipewire_latency_ms` | 8 | 1–40 | `PW_KEY_NODE_LATENCY` hint passed to the compositor. |
| `cpu_pinning` | true | bool | Capture thread uses `SCHED_RR` priority 10 + physical-core affinity. |
| `enet_4mib_buffer` | true | bool | ENet socket buffers grown to 4 MiB (overrides kernel default). |

The `cpu_pinning` and `enet_4mib_buffer` knobs fall back to upstream
defaults when set to `false`. The other three are passed through.

## License

GPL-3.0-only, inherited from upstream LizardByte/Sunshine.

## Credits

- LizardByte maintainers and contributors. The upstream Sunshine project is
  theirs.
- LizardByte. The web UI, Flatpak packaging, Windows installer, macOS .dmg,
  FreeBSD .pkg, AUR package, Homebrew formula, cross-platform plumbing.

## See also

- `docs/PORTING.md`
- `docs/CONFIGURATION.md`
- `scripts/cachyos-build.sh`
- [LizardByte/Sunshine README](https://github.com/LizardByte/Sunshine)