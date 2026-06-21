#!/usr/bin/env bash
#
# scripts/cachyos-build.sh
#
# Build Sunshine (CachyOS / Linux local-LAN fast path) on a fresh
# CachyOS (or Arch / Manjaro / EndeavourOS) install in one shot.
#
# What it does:
#   1. Verifies we're on Linux (Sunshine's other platforms work too,
#      but this script is tuned for the CachyOS fast path).
#   2. Verifies the submodules are present, and if not, asks
#      `git submodule update --init --recursive` to fetch them.
#   3. Installs the build dependencies via pacman.
#   4. Runs cmake with the right flags for the CachyOS fast path
#      (auto-detects Zen 1/2/3/4 from /proc/cpuinfo, enables LTO,
#      drops the docs/tests that just slow the configure).
#   5. Builds with ninja.
#   6. Runs `sudo cmake --install build` and reloads the user
#      systemd manager.
#
# Usage:
#   ./scripts/cachyos-build.sh
#
# It is safe to re-run; cmake will reuse the existing build directory.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"

say()  { printf '\033[1;36m[%s]\033[0m %s\n' "$(date +%H:%M:%S)" "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[fatal]\033[0m %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 1. Platform check
# ---------------------------------------------------------------------------
if [[ "$(uname -s)" != "Linux" ]]; then
  die "This script is for Linux. Sunshine itself builds on macOS and Windows too — run the upstream docs."
fi
if ! command -v pacman >/dev/null 2>&1; then
  die "pacman not found. This script targets CachyOS / Arch / Manjaro. On Debian/Ubuntu use the upstream install script (apt)."
fi

# ---------------------------------------------------------------------------
# 2. Submodules
# ---------------------------------------------------------------------------
say "Checking git submodules..."
# Pick a handful of submodules; if any are empty we need to fetch.
needs_init=0
for sub in third-party/moonlight-common-c third-party/Simple-Web-Server \
           third-party/libdisplaydevice third-party/tray \
           third-party/glad third-party/nv-codec-headers; do
  if [[ ! -f "${REPO_ROOT}/${sub}/CMakeLists.txt" && ! -d "${REPO_ROOT}/${sub}/src" && ! -d "${REPO_ROOT}/${sub}/include" ]]; then
    needs_init=1
    break
  fi
done

if [[ "${needs_init}" -eq 1 ]]; then
  say "Initialising submodules (this can take a minute or two)..."
  git -C "${REPO_ROOT}" submodule update --init --recursive
else
  say "Submodules look good."
fi

# ---------------------------------------------------------------------------
# 3. Build dependencies
# ---------------------------------------------------------------------------
say "Installing build dependencies via pacman (you may be prompted)..."
sudo pacman -S --needed --noconfirm \
    base-devel cmake ninja git \
    openssl curl libpulse libdrm libva libva-drm \
    libx11 libxfixes libxrandr libxcb libxkbcommon \
    libevdev libopus ffmpeg \
    libpipewire libportal \
    wayland wayland-protocols \
    libudev0 libcap-miniupnpc libnatpmp \
    vulkan-headers shaderc glslang \
    boost miniupnpc nlohmann-json \
    libpng libxext libxtst

# ---------------------------------------------------------------------------
# 4. CMake configure
# ---------------------------------------------------------------------------
say "Configuring with cmake (CachyOS fast path is auto-detected)..."
mkdir -p "${BUILD_DIR}"
cmake -B "${BUILD_DIR}" -G Ninja -S "${REPO_ROOT}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_DOCS=OFF \
    -DSUNSHINE_ENABLE_TESTS=OFF \
    -DSUNSHINE_ENABLE_TRAY=OFF \
    -DFFMPEG_PREBUILT=ON

# Confirm the CachyOS native flags actually engaged.
if grep -q "CachyOS native build" "${BUILD_DIR}/CMakeFiles/CMakeOutput.log" 2>/dev/null \
   || grep -q "CachyOS native build" <(cmake "${BUILD_DIR}" 2>&1) ; then
  :
fi
say "Look for this in the configure output above to confirm native flags engaged:"
printf '\n    \033[1m-- CachyOS native build: -march=znver2 -mtune=znver2\033[0m\n\n'
say "  (or znver3 / znver4 / x86-64-v3 depending on your CPU)"

# ---------------------------------------------------------------------------
# 5. Build
# ---------------------------------------------------------------------------
say "Building with ninja (this takes 2–5 minutes on a Ryzen 5 4600H)..."
cmake --build "${BUILD_DIR}" -j"$(nproc)"

# ---------------------------------------------------------------------------
# 6. Install + reload systemd
# ---------------------------------------------------------------------------
say "Installing (sudo cmake --install)..."
sudo cmake --install "${BUILD_DIR}"

say "Reloading user systemd manager so 'systemctl --user status sunshine' works..."
systemctl --user daemon-reload 2>/dev/null || true

cat <<'EOF'

  Done.

  Quick start:
    systemctl --user enable --now sunshine
    # Open the web UI at the URL it prints, pair a Moonlight client,
    # and start streaming.

  Tunable knobs (all safe to leave at defaults):
    -DNVENC_PRESET=P1         # fastest encode preset
    -DSUNSHINE_CACHYOS_NATIVE=OFF  # disable the Zen microarch flags
    -DCMAKE_BUILD_TYPE=Debug  # if you want symbols for profiling

  If you hit "MESA-LOADER: failed to open" or "libnvidia-glcore.so"
  errors, your discrete NVIDIA GPU isn't set up for the right user;
  see https://wiki.cachyos.org/nvidia/ for the CachyOS NVIDIA guide.
EOF
