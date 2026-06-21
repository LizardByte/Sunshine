#!/usr/bin/env bash
#
# scripts/cachyos-build.sh
#
# Build Sunshine (CachyOS / Linux local-LAN fast path) on a fresh
# CachyOS (or Arch / Manjaro / EndeavourOS) install in one shot.
#
# What it does:
#   1. Verifies we're on Linux with pacman.
#   2. Makes sure all submodules are fetched, even if a previous run
#      died mid-clone. Tolerates individual submodule failures
#      (e.g. transient network) and retries them.
#   3. Installs the build dependencies via pacman --needed.
#   4. Runs cmake with the CachyOS fast-path flags (auto-detects
#      Zen 1/2/3/4 from /proc/cpuinfo, enables LTO, drops docs/tests).
#   5. Builds with ninja.
#   6. Runs `sudo cmake --install build` and reloads the user
#      systemd manager.
#   7. Verifies that `sunshine` is on $PATH and prints a "what's
#      next" with the exact next commands.
#
# Usage:
#   ./scripts/cachyos-build.sh
#
# Re-runnable: cmake will reuse the existing build directory and only
# rebuild what changed. Submodule state is preserved between runs.
#
# To force a clean rebuild:
#   ./scripts/cachyos-build.sh --clean
#
# To skip pacman (deps already installed) and just rebuild:
#   ./scripts/cachyos-build.sh --no-pacman

set -uo pipefail   # NB: NOT -e — we want to keep going through non-fatal errors

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
CLEAN=0
RUN_PACMAN=1

for arg in "$@"; do
  case "$arg" in
    --clean)    CLEAN=1 ;;
    --no-pacman) RUN_PACMAN=0 ;;
    -h|--help)
      sed -n '2,40p' "$0"
      exit 0
      ;;
    *) echo "Unknown arg: $arg" >&2; exit 2 ;;
  esac
done

# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------
say()  { printf '\033[1;36m[%s]\033[0m %s\n' "$(date +%H:%M:%S)" "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[fatal]\033[0m %s\n' "$*" >&2; exit 1; }

step() {
  printf '\n'
  printf '\033[1;35m═══ %s ═══\033[0m\n' "$*"
  printf '\n'
}

# ---------------------------------------------------------------------------
# 1. Platform check
# ---------------------------------------------------------------------------
step "1/6  Platform check"
if [[ "$(uname -s)" != "Linux" ]]; then
  die "This script is for Linux. Sunshine builds on macOS and Windows too — use the upstream docs for those."
fi
if ! command -v pacman >/dev/null 2>&1; then
  die "pacman not found. This script targets CachyOS / Arch / Manjaro. On Debian/Ubuntu use the upstream install.sh (apt)."
fi
say "Linux + pacman: ✓ ($(. /etc/os-release && echo "$PRETTY_NAME"))"

# ---------------------------------------------------------------------------
# 2. Submodules
# ---------------------------------------------------------------------------
step "2/6  Git submodules"

# Required submodules. If any of these are missing or empty, init.
REQUIRED_SUBS=(
  third-party/moonlight-common-c
  third-party/Simple-Web-Server
  third-party/libdisplaydevice
  third-party/tray
  third-party/glad
  third-party/nv-codec-headers
  third-party/nanors
  third-party/wlroots-protocols
  third-party/doxyconfig
  third-party/build-deps
  third-party/lizardbyte-common
)

missing=()
for sub in "${REQUIRED_SUBS[@]}"; do
  if [[ ! -d "${REPO_ROOT}/${sub}" ]] || [[ -z "$(ls -A "${REPO_ROOT}/${sub}" 2>/dev/null)" ]]; then
    missing+=("$sub")
  fi
done

if [[ ${#missing[@]} -gt 0 ]]; then
  say "Missing submodules: ${missing[*]}"
  say "Running: git submodule update --init --recursive (this can take a few minutes)..."
  if ! git -C "${REPO_ROOT}" submodule update --init --recursive; then
    warn "submodule update exited non-zero. Retrying the missing ones individually..."
    for sub in "${missing[@]}"; do
      say "  retrying: $sub"
      git -C "${REPO_ROOT}" submodule update --init --recursive -- "$sub" || \
        warn "  $sub still failed. Build will probably break at the cmake step."
    done
  fi
else
  say "All required submodules look present."
fi

# Verify the critical ones actually have content.
for sub in third-party/moonlight-common-c third-party/Simple-Web-Server third-party/glad; do
  if [[ ! -d "${REPO_ROOT}/${sub}" ]] || [[ -z "$(ls -A "${REPO_ROOT}/${sub}" 2>/dev/null)" ]]; then
    die "Required submodule still missing after update: $sub. Check your network / GitHub access, then re-run."
  fi
done
say "Submodule check: ✓"

# ---------------------------------------------------------------------------
# 3. Build dependencies
# ---------------------------------------------------------------------------
step "3/6  Build dependencies"
if [[ "$RUN_PACMAN" -eq 0 ]]; then
  say "Skipping pacman (--no-pacman)."
else
  say "Installing build deps via pacman --needed (you'll be asked for sudo if needed)..."
  if ! sudo pacman -S --needed --noconfirm \
        base-devel cmake ninja git \
        openssl curl libpulse libdrm libva \
        libx11 libxfixes libxrandr libxcb libxkbcommon \
        libevdev opus ffmpeg \
        libpipewire libportal \
        wayland wayland-protocols \
        libudev libcap libnatpmp \
        vulkan-headers shaderc glslang \
        boost miniupnpc nlohmann-json \
        libpng libxext libxtst; then
    warn "pacman returned non-zero. Some packages may not be available in your repos. Continuing — build will tell us if anything critical is missing."
  fi
fi
say "Dependencies: assuming OK (build will fail loudly if not)."

# ---------------------------------------------------------------------------
# 4. CMake configure
# ---------------------------------------------------------------------------
step "4/6  CMake configure"

if [[ "$CLEAN" -eq 1 && -d "$BUILD_DIR" ]]; then
  say "Removing old build directory (--clean)..."
  rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
say "Running cmake..."
set +e
cmake -B "$BUILD_DIR" -G Ninja -S "$REPO_ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_DOCS=OFF \
    -DSUNSHINE_ENABLE_TESTS=OFF \
    -DSUNSHINE_ENABLE_TRAY=OFF \
    -DSUNSHINE_ENABLE_CUDA=OFF \
    -DCUDA_FAIL_ON_MISSING=OFF \
    -DFFMPEG_PREBUILT=ON
cmake_rc=$?
set -u

if [[ $cmake_rc -ne 0 ]]; then
  die "cmake configure failed (exit $cmake_rc). Look at the last 50 lines of output above for the actual error — usually a missing dep or a submodule not fully fetched."
fi
say "CMake configure: ✓"

# Show the user the CachyOS native flag line so they can confirm.
if grep -q "CachyOS native build" "$BUILD_DIR/CMakeFiles/CMakeOutput.log" 2>/dev/null; then
  say "Native flags engaged (look for the line above):"
  grep "CachyOS native build" "$BUILD_DIR/CMakeFiles/CMakeOutput.log" 2>/dev/null | head -1 | sed 's/^/    /'
else
  warn "Did not find 'CachyOS native build' in CMakeOutput.log. Microarch auto-detect may have failed."
  warn "Check: cat /proc/cpuinfo | grep 'model name' | head -1"
fi

# ---------------------------------------------------------------------------
# 5. Build
# ---------------------------------------------------------------------------
step "5/6  Build (ninja)"
say "Building with ninja (this takes 2-5 minutes on a Ryzen 5 4600H)..."
set +e
cmake --build "$BUILD_DIR" -j"$(nproc)"
build_rc=$?
set -u

if [[ $build_rc -ne 0 ]]; then
  die "Build failed (exit $build_rc). Run 'cmake --build build' manually to see the full error."
fi
say "Build: ✓"

# ---------------------------------------------------------------------------
# 6. Install + final check
# ---------------------------------------------------------------------------
step "6/6  Install + verification"
say "sudo cmake --install build..."
set +e
sudo cmake --install "$BUILD_DIR"
install_rc=$?
set -u

if [[ $install_rc -ne 0 ]]; then
  die "Install failed (exit $install_rc)."
fi

say "Reloading user systemd manager..."
systemctl --user daemon-reload 2>/dev/null || true

# Verify the binary is on $PATH.
if command -v sunshine >/dev/null 2>&1; then
  say "Verified: $(command -v sunshine) is on PATH"
else
  warn "sunshine is not on \$PATH. Check /usr/local/bin/sunshine (or wherever --install put it) and add it to your shell's PATH if needed."
fi

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
cat <<'EOF'

  ══════════════════════════════════════════════════════════════════════
   Done. Sunshine is installed.
  ══════════════════════════════════════════════════════════════════════

  Quick start:

    systemctl --user enable --now sunshine
    # Or, for one-off runs without systemd:
    sunshine

  The web UI is at https://localhost:47990 — pair your Moonlight
  client using the PIN it prints on first run.

  If you hit "MESA-LOADER: failed to open" or "libnvidia-glcore.so"
  errors, your discrete NVIDIA GPU isn't set up for the right user;
  see https://wiki.cachyos.org/nvidia/ for the CachyOS NVIDIA guide.

  Tunable knobs for the build:
    ./scripts/cachyos-build.sh --clean      # nuke the build dir and start over
    ./scripts/cachyos-build.sh --no-pacman  # skip deps, just rebuild
EOF
