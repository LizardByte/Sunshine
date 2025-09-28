#!/bin/bash
set -e

# Version requirements - centralized for easy maintenance
cmake_min="3.25.0"
target_cmake_version="3.30.1"
doxygen_min="1.10.0"
_doxygen_min="${doxygen_min//\./_}"  # Convert dots to underscores for URL
doxygen_max="1.12.0"

# Default value for arguments
appimage_build=0
cross_compile=0
cuda_patches=0
num_processors=$(nproc)
publisher_name="Third Party Publisher"
publisher_website=""
publisher_issue_url="https://app.lizardbyte.dev/support"
skip_cleanup=0
skip_cuda=0
skip_libva=0
skip_package=0
sudo_cmd="sudo"
target_arch=""
target_tuple=""
ubuntu_test_repo=0
use_aptitude=0
update_sources=0
step="all"

# common variables
gcc_alternative_files=(
  "gcc"
  "g++"
  "gcov"
  "gcc-ar"
  "gcc-ranlib"
)

# Reusable function to detect nvcc path
function detect_nvcc_path() {
  local nvcc_path=""

  # First check for system-installed CUDA
  nvcc_path=$(command -v nvcc 2>/dev/null) || true
  if [ -n "$nvcc_path" ]; then
    echo "$nvcc_path"
    return 0
  fi

  # Then check for locally installed CUDA in build directory
  if [ -f "${build_dir}/cuda/bin/nvcc" ]; then
    echo "${build_dir}/cuda/bin/nvcc"
    return 0
  fi

  # No CUDA found
  return 1
}

# Reusable function to setup NVM environment
function setup_nvm_environment() {
  # Only setup NVM if it should be used for this distro
  if [ "$nvm_node" == 1 ]; then
    # Check if NVM is installed and source it
    if [ -f "$HOME/.nvm/nvm.sh" ]; then
      # shellcheck source=/dev/null
      source "$HOME/.nvm/nvm.sh"
      # Use the default node version installed by NVM
      nvm use default 2>/dev/null || nvm use node 2>/dev/null || true
      echo "Using NVM Node.js version: $(node --version 2>/dev/null || echo 'not available')"
      echo "Using NVM npm version: $(npm --version 2>/dev/null || echo 'not available')"
    else
      echo "NVM not found, using system Node.js if available"
    fi
  fi
}

function _usage() {
  local exit_code=$1

  cat <<EOF
This script installs the dependencies and builds the project.
The script is intended to be run on a Debian-based or Fedora-based system.

Usage:
  $0 [options]

Options:
  -h, --help               Display this help message.
  -s, --sudo-off           Disable sudo command.
  --appimage-build         Compile for AppImage, this will not create the AppImage, just the executable.
  --cross-compile          Enable cross compilation.
  --cuda-patches           Apply cuda patches.
  --num-processors         The number of processors to use for compilation. Default is the value of 'nproc'.
  --publisher-name         The name of the publisher (not developer) of the application.
  --publisher-website      The URL of the publisher's website.
  --publisher-issue-url    The URL of the publisher's support site or issue tracker.
                           If you provide a modified version of Sunshine, we kindly request that you use your own url.
  --skip-cleanup           Do not restore the original gcc alternatives, or the math-vector.h file.
  --skip-cuda              Skip CUDA installation.
  --skip-libva             Skip libva installation. This will automatically be enabled if passing --appimage-build.
  --skip-package           Skip creating DEB, or RPM package.
  --target-arch            Target architecture for cross compilation (e.g., arm64, amd64).
  --target-tuple           Target tuple for cross compilation (e.g., aarch64-linux-gnu, x86_64-linux-gnu).
  --ubuntu-test-repo       Install ppa:ubuntu-toolchain-r/test repo on Ubuntu.
  --use-aptitude           Use aptitude instead of apt for package management on Debian/Ubuntu systems.
  --update-sources         Update Ubuntu sources.list for cross-compilation (Ubuntu only, off by default).
  --step                   Which step(s) to run: deps, cmake, validation, build, package, cleanup, or all (default: all)

Steps:
  deps                     Install dependencies only
  cmake                    Run cmake configure only
  validation               Run validation commands only
  build                    Build the project only
  package                  Create packages only
  cleanup                  Cleanup alternatives and backups only
  all                      Run all steps (default)
EOF

  exit "$exit_code"
}

# Parse named arguments
while getopts ":hs-:" opt; do
  case ${opt} in
    h ) _usage 0 ;;
    s ) sudo_cmd="" ;;
    - )
      case "${OPTARG}" in
        help) _usage 0 ;;
        appimage-build)
          appimage_build=1
          skip_libva=1
          ;;
        cross-compile) cross_compile=1 ;;
        cuda-patches)
          cuda_patches=1
          ;;
        num-processors=*)
          num_processors="${OPTARG#*=}"
          ;;
        publisher-name=*)
          publisher_name="${OPTARG#*=}"
          ;;
        publisher-website=*)
          publisher_website="${OPTARG#*=}"
          ;;
        publisher-issue-url=*)
          publisher_issue_url="${OPTARG#*=}"
          ;;
        skip-cleanup) skip_cleanup=1 ;;
        skip-cuda) skip_cuda=1 ;;
        skip-libva) skip_libva=1 ;;
        skip-package) skip_package=1 ;;
        sudo-off) sudo_cmd="" ;;
        target-arch=*)
          target_arch="${OPTARG#*=}"
          ;;
        target-tuple=*)
          target_tuple="${OPTARG#*=}"
          ;;
        ubuntu-test-repo) ubuntu_test_repo=1 ;;
        use-aptitude) use_aptitude=1 ;;
        update-sources) update_sources=1 ;;
        step=*)
          step="${OPTARG#*=}"
          ;;
        *)
          echo "Invalid option: --${OPTARG}" 1>&2
          _usage 1
          ;;
      esac
      ;;
    \? )
      echo "Invalid option: -${OPTARG}" 1>&2
      _usage 1
      ;;
  esac
done
shift $((OPTIND -1))

# dependencies array to build out
dependencies=()

function add_arch_deps() {
  dependencies+=(
    'avahi'
    'base-devel'
    'cmake'
    'curl'
    'doxygen'
    "gcc${gcc_version}"
    "gcc${gcc_version}-libs"
    'git'
    'graphviz'
    'libayatana-appindicator'
    'libcap'
    'libdrm'
    'libevdev'
    'libmfx'
    'libnotify'
    'libpulse'
    'libva'
    'libx11'
    'libxcb'
    'libxfixes'
    'libxrandr'
    'libxtst'
    'miniupnpc'
    'ninja'
    'nodejs'
    'npm'
    'numactl'
    'openssl'
    'opus'
    'udev'
    'wayland'
  )

  if [ "$skip_libva" == 0 ]; then
    dependencies+=(
      "libva"  # VA-API
    )
  fi

  if [ "$skip_cuda" == 0 ]; then
    dependencies+=(
      "cuda"  # VA-API
    )
  fi
}

function add_debian_based_deps() {
  dependencies+=(
    "appstream"
    "appstream-util"
    "bison"  # required if we need to compile doxygen
    "build-essential"
    "cmake"
    "desktop-file-utils"
    "doxygen"
    "flex"  # required if we need to compile doxygen
    "git"
    "graphviz"
    "ninja-build"
    "npm"  # web-ui
    "udev"
    "wget"  # necessary for cuda install with `run` file
    "xvfb"  # necessary for headless unit testing
  )


  if [ "$cross_compile" == 1 ] && [ -n "$target_arch" ]; then
    # Cross-compile specific library packages with architecture suffix
    dependencies+=(
      "crossbuild-essential-${target_arch}"
      "gcc-${gcc_version}-aarch64-linux-gnu"
      "g++-${gcc_version}-aarch64-linux-gnu"
      "libcap-dev:${target_arch}"  # KMS
      "libcurl4-openssl-dev:${target_arch}"
      "libdrm-dev:${target_arch}"  # KMS
      "libevdev-dev:${target_arch}"
      "libgbm-dev:${target_arch}"
      "libminiupnpc-dev:${target_arch}"
      "libnotify-dev:${target_arch}"
      "libnuma-dev:${target_arch}"
      "libopus-dev:${target_arch}"
      "libpulse-dev:${target_arch}"
      "libssl-dev:${target_arch}"
      "libwayland-dev:${target_arch}"  # Wayland
      "libx11-dev:${target_arch}"  # X11
      "libxcb-shm0-dev:${target_arch}"  # X11
      "libxcb-xfixes0-dev:${target_arch}"  # X11
      "libxcb1-dev:${target_arch}"  # X11
      "libxfixes-dev:${target_arch}"  # X11
      "libxrandr-dev:${target_arch}"  # X11
      "libxtst-dev:${target_arch}"  # X11
    )
  else
    # Native build
    dependencies+=(
      "gcc-${gcc_version}"
      "g++-${gcc_version}"
      "libcap-dev"  # KMS
      "libcurl4-openssl-dev"
      "libdrm-dev"  # KMS
      "libevdev-dev"
      "libgbm-dev"
      "libminiupnpc-dev"
      "libnotify-dev"
      "libnuma-dev"
      "libopus-dev"
      "libpulse-dev"
      "libssl-dev"
      "libwayland-dev"  # Wayland
      "libx11-dev"  # X11
      "libxcb-shm0-dev"  # X11
      "libxcb-xfixes0-dev"  # X11
      "libxcb1-dev"  # X11
      "libxfixes-dev"  # X11
      "libxrandr-dev"  # X11
      "libxtst-dev"  # X11
    )
  fi

  if [ "$skip_libva" == 0 ]; then
    if [ "$cross_compile" == 1 ] && [ -n "$target_arch" ]; then
      dependencies+=(
        "libva-dev:${target_arch}"  # VA-API
      )
    else
      dependencies+=(
        "libva-dev"  # VA-API
      )
    fi
  fi
}

function add_test_ppa() {
  if [ "$ubuntu_test_repo" == 1 ]; then
    $package_install_command "software-properties-common"
    ${sudo_cmd} add-apt-repository ppa:ubuntu-toolchain-r/test -y
  fi
}

function add_debian_deps() {
  add_test_ppa
  add_debian_based_deps
  if [ "$cross_compile" == 1 ] && [ -n "$target_arch" ]; then
    dependencies+=(
      "libayatana-appindicator3-dev:${target_arch}"
    )
  else
    dependencies+=(
      "libayatana-appindicator3-dev"
    )
  fi
}

function add_ubuntu_deps() {
  add_test_ppa
  add_debian_based_deps
  if [ "$cross_compile" == 1 ] && [ -n "$target_arch" ]; then
    dependencies+=(
      "libappindicator3-dev:${target_arch}"
    )
  else
    dependencies+=(
      "libappindicator3-dev"
    )
  fi
}

function add_fedora_deps() {
  dependencies+=(
    "appstream"
    "cmake"
    "desktop-file-utils"
    "doxygen"
    "gcc${gcc_version}"
    "gcc${gcc_version}-c++"
    "git"
    "graphviz"
    "libappindicator-gtk3-devel"
    "libappstream-glib"
    "libcap-devel"
    "libcurl-devel"
    "libdrm-devel"
    "libevdev-devel"
    "libnotify-devel"
    "libX11-devel"  # X11
    "libxcb-devel"  # X11
    "libXcursor-devel"  # X11
    "libXfixes-devel"  # X11
    "libXi-devel"  # X11
    "libXinerama-devel"  # X11
    "libXrandr-devel"  # X11
    "libXtst-devel"  # X11
    "mesa-libGL-devel"
    "mesa-libgbm-devel"
    "miniupnpc-devel"
    "ninja-build"
    "npm"
    "numactl-devel"
    "openssl-devel"
    "opus-devel"
    "pulseaudio-libs-devel"
    "rpm-build"  # if you want to build an RPM binary package
    "wget"  # necessary for cuda install with `run` file
    "which"  # necessary for cuda install with `run` file
    "xorg-x11-server-Xvfb"  # necessary for headless unit testing
  )

  if [ "$skip_libva" == 0 ]; then
    dependencies+=(
      "libva-devel"  # VA-API
    )
  fi
}

function install_cuda() {
  # Check if CUDA is already available
  if detect_nvcc_path > /dev/null 2>&1; then
    return
  fi

  local cuda_prefix="https://developer.download.nvidia.com/compute/cuda/"
  local cuda_suffix=""
  if [ "$architecture" == "aarch64" ]; then
    local cuda_suffix="_sbsa"
  fi

  if [ "$architecture" == "aarch64" ]; then
    # we need to patch the math-vector.h file for aarch64 fedora
    # back up /usr/include/bits/math-vector.h
    math_vector_file=""
    if [ "$distro" == "ubuntu" ] || [ "$version" == "24.04" ]; then
      math_vector_file="/usr/include/aarch64-linux-gnu/bits/math-vector.h"
    elif [ "$distro" == "fedora" ]; then
      math_vector_file="/usr/include/bits/math-vector.h"
    fi

    if [ -n "$math_vector_file" ]; then
      # patch headers https://bugs.launchpad.net/ubuntu/+source/mumax3/+bug/2032624
      ${sudo_cmd} cp "$math_vector_file" "$math_vector_file.bak"
      ${sudo_cmd} sed -i 's/__Float32x4_t/int/g' "$math_vector_file"
      ${sudo_cmd} sed -i 's/__Float64x2_t/int/g' "$math_vector_file"
      ${sudo_cmd} sed -i 's/__SVFloat32_t/float/g' "$math_vector_file"
      ${sudo_cmd} sed -i 's/__SVFloat64_t/float/g' "$math_vector_file"
      ${sudo_cmd} sed -i 's/__SVBool_t/int/g' "$math_vector_file"
    fi
  fi

  local url="${cuda_prefix}${cuda_version}/local_installers/cuda_${cuda_version}_${cuda_build}_linux${cuda_suffix}.run"
  echo "cuda url: ${url}"
  wget "$url" --progress=bar:force:noscroll -q --show-progress -O "${build_dir}/cuda.run"
  chmod a+x "${build_dir}/cuda.run"
  "${build_dir}/cuda.run" --silent --toolkit --toolkitpath="${build_dir}/cuda" --no-opengl-libs --no-man-page --no-drm
  rm "${build_dir}/cuda.run"

  # run cuda patches
  if [ "$cuda_patches" == 1 ]; then
    echo "Applying CUDA patches"
    local patch_dir="${script_dir}/../packaging/linux/patches/${architecture}"
    if [ -d "$patch_dir" ]; then
      for patch in "$patch_dir"/*.patch; do
        echo "Applying patch: $patch"
        patch -p2 \
          --backup \
          --directory="${build_dir}/cuda" \
          --verbose \
          < "$patch"
      done
    else
      echo "No patches found for architecture: $architecture"
    fi
  fi
}

function check_version() {
  local package_name=$1
  local min_version=$2
  local max_version=$3
  local installed_version

  echo "Checking if $package_name is installed and at least version $min_version"

  if [ "$distro" == "debian" ] || [ "$distro" == "ubuntu" ]; then
    installed_version=$(dpkg -s "$package_name" 2>/dev/null | grep '^Version:' | awk '{print $2}')
  elif [ "$distro" == "fedora" ]; then
    installed_version=$(rpm -q --queryformat '%{VERSION}' "$package_name" 2>/dev/null)
  elif [ "$distro" == "arch" ]; then
    installed_version=$(pacman -Q "$package_name" | awk '{print $2}' )
  else
    echo "Unsupported Distro"
    return 1
  fi

  if [ -z "$installed_version" ]; then
    echo "Package not installed"
    return 1
  fi

if [[ "$(printf '%s\n' "$installed_version" "$min_version" | sort -V | head -n1)" = "$min_version" ]] && \
   [[ "$(printf '%s\n' "$installed_version" "$max_version" | sort -V | head -n1)" = "$installed_version" ]]; then
    echo "Installed version is within range"
    return 0
  else
    echo "$package_name version $installed_version is out of range"
    return 1
  fi
}

function update_ubuntu_sources() {
  echo "Updating Ubuntu sources for cross-compilation..."

  if [ "$distro" != "ubuntu" ]; then
    echo "Sources update only supported on Ubuntu, skipping..."
    return
  fi

  if [ "$cross_compile" != 1 ] || [ -z "$target_arch" ]; then
    echo "Not cross-compiling, skipping sources update..."
    return
  fi

  # Install ca-certificates first to avoid SSL issues
  echo "Installing ca-certificates for HTTPS repositories..."
  ${sudo_cmd} apt-get update || true  # Allow this to fail initially
  ${sudo_cmd} apt-get install -y ca-certificates || true

  # Use existing distribution detection variables
  local dist_name
  local ubuntu_version
  local ubuntu_major_version

  # Extract codename from /etc/os-release
  dist_name=$(grep '^VERSION_CODENAME=' /etc/os-release | cut -d'=' -f2 | tr -d '"')

  # Use the version variable already set by main detection logic
  ubuntu_version="$version"
  ubuntu_major_version=${ubuntu_version%%.*}

  echo "Detected Ubuntu distribution: $dist_name"
  echo "Detected Ubuntu version: $ubuntu_version"
  echo "Detected Ubuntu major version: $ubuntu_major_version"

  # Determine mirror URLs
  local main_mirror="http://archive.ubuntu.com/ubuntu"
  local ports_mirror="http://ports.ubuntu.com/ubuntu-ports"
  local security_mirror="http://security.ubuntu.com/ubuntu"

  # Add target architecture
  echo "Adding architecture: $target_arch"
  ${sudo_cmd} dpkg --add-architecture "$target_arch"

  # Determine source file location based on Ubuntu version
  local source_file
  if [[ $ubuntu_major_version -ge 24 ]]; then
    source_file="/etc/apt/sources.list.d/ubuntu.sources"
  else
    source_file="/etc/apt/sources.list"
  fi

  echo "Using sources file: $source_file"

  # Backup original sources
  echo "Backing up original sources file..."
  ${sudo_cmd} cp "$source_file" "${source_file}.bak"

  # Print original sources for debugging
  echo "Original sources:"
  ${sudo_cmd} cat "$source_file"
  echo "----"

  # Update sources based on Ubuntu version
  if [[ $ubuntu_major_version -ge 24 ]]; then
    # Ubuntu 24.04+ uses the new .sources format
    local new_sources
    new_sources=$(cat <<VAREOF
# Main Ubuntu repository for native architecture
Types: deb
URIs: ${main_mirror}
Suites: ${dist_name} ${dist_name}-updates ${dist_name}-backports
Components: main universe restricted multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
Architectures: $(dpkg --print-architecture)

# Ubuntu security updates for native architecture
Types: deb
URIs: ${security_mirror}
Suites: ${dist_name}-security
Components: main universe restricted multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
Architectures: $(dpkg --print-architecture)

# Ubuntu ports repository for cross-compilation architecture
Types: deb
URIs: ${ports_mirror}
Suites: ${dist_name} ${dist_name}-updates ${dist_name}-backports ${dist_name}-security
Components: main universe restricted multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
Architectures: ${target_arch}
VAREOF
)
    echo "$new_sources" | ${sudo_cmd} tee "$source_file" > /dev/null
  else
    # Ubuntu 22.04 and earlier use the traditional sources.list format
    # Fix original sources to specify amd64 architecture
    ${sudo_cmd} sed -i -e "s#deb mirror#deb [arch=$(dpkg --print-architecture)] mirror#g" "$source_file"

    # Add cross-compilation sources
    local extra_sources
    extra_sources=$(cat <<VAREOF
deb [arch=${target_arch}] ${ports_mirror} ${dist_name} main restricted
deb [arch=${target_arch}] ${ports_mirror} ${dist_name}-updates main restricted
deb [arch=${target_arch}] ${ports_mirror} ${dist_name} universe
deb [arch=${target_arch}] ${ports_mirror} ${dist_name}-updates universe
deb [arch=${target_arch}] ${ports_mirror} ${dist_name} multiverse
deb [arch=${target_arch}] ${ports_mirror} ${dist_name}-updates multiverse
deb [arch=${target_arch}] ${ports_mirror} ${dist_name}-backports main restricted universe multiverse
deb [arch=${target_arch}] ${ports_mirror} ${dist_name}-security main restricted
deb [arch=${target_arch}] ${ports_mirror} ${dist_name}-security universe
deb [arch=${target_arch}] ${ports_mirror} ${dist_name}-security multiverse
VAREOF
)
    echo "$extra_sources" | ${sudo_cmd} tee -a "$source_file" > /dev/null
  fi

  echo "----"
  echo "Updated sources:"
  ${sudo_cmd} cat "$source_file"
  echo "----"

  echo "Ubuntu sources updated successfully for cross-compilation"
}

function run_step_deps() {
  echo "Running step: Install dependencies"

  # Update Ubuntu sources for cross-compilation if requested
  if [ "$update_sources" == 1 ]; then
    update_ubuntu_sources
  fi

  # Update the package list using apt-get first (even if aptitude is requested)
  if [ "$distro" == "debian" ] || [ "$distro" == "ubuntu" ]; then
    ${sudo_cmd} apt-get update
  else
    # For non-Debian systems, use their standard update command
    $package_update_command
  fi

  # Install aptitude first if requested for Debian/Ubuntu systems
  if [ "$use_aptitude" == 1 ] && ([ "$distro" == "debian" ] || [ "$distro" == "ubuntu" ]); then
    echo "Installing aptitude package manager..."
    ${sudo_cmd} apt-get install -y aptitude
    echo "Aptitude installed successfully"

    # Now update with aptitude
    echo "Updating package lists with aptitude..."
    ${sudo_cmd} aptitude update
  fi

  if [ "$distro" == "arch" ]; then
    add_arch_deps
  elif [ "$distro" == "debian" ]; then
    add_debian_deps
  elif [ "$distro" == "ubuntu" ]; then
    add_ubuntu_deps
  elif [ "$distro" == "fedora" ]; then
    add_fedora_deps
    ${sudo_cmd} dnf group install "$dev_tools_group" -y
  fi

  # Install the dependencies
  $package_install_command "${dependencies[@]}"

  # reload the environment
  # shellcheck source=/dev/null
  source ~/.bashrc

  #set gcc version based on distros
  if [ "$distro" == "arch" ]; then
    export CC=gcc-14
    export CXX=g++-14
  elif [ "$distro" == "debian" ] || [ "$distro" == "ubuntu" ]; then
    # Export GCC version for toolchain files
    export LINUX_GCC_VERSION="$gcc_version"

    # Only set up alternatives for native compilation
    # For cross-compilation, the toolchain files will handle compiler selection
    if [ "$cross_compile" == 0 ]; then
      for file in "${gcc_alternative_files[@]}"; do
        file_path="/etc/alternatives/$file"
        if [ -e "$file_path" ]; then
          ${sudo_cmd} mv "$file_path" "$file_path.bak"
        fi
      done

      ${sudo_cmd} update-alternatives --install \
        /usr/bin/gcc gcc /usr/bin/gcc-${gcc_version} 100 \
        --slave /usr/bin/g++ g++ /usr/bin/g++-${gcc_version} \
        --slave /usr/bin/gcov gcov /usr/bin/gcov-${gcc_version} \
        --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-${gcc_version} \
        --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-${gcc_version}
    fi
  fi

  # compile cmake if the version is too low
  if ! check_version "cmake" "$cmake_min" "inf"; then
    cmake_prefix="https://github.com/Kitware/CMake/releases/download/v"
    if [ "$architecture" == "x86_64" ]; then
      cmake_arch="x86_64"
    elif [ "$architecture" == "aarch64" ]; then
      cmake_arch="aarch64"
    fi
    url="${cmake_prefix}${target_cmake_version}/cmake-${target_cmake_version}-linux-${cmake_arch}.sh"
    echo "cmake url: ${url}"
    wget "$url" --progress=bar:force:noscroll -q --show-progress -O "${build_dir}/cmake.sh"
    ${sudo_cmd} sh "${build_dir}/cmake.sh" --skip-license --prefix=/usr/local
    echo "cmake installed, version:"
    cmake --version
  fi

  # compile doxygen if version is too low
  if ! check_version "doxygen" "$doxygen_min" "$doxygen_max"; then
    if [ "${SUNSHINE_COMPILE_DOXYGEN}" == "true" ]; then
      echo "Compiling doxygen"
      doxygen_url="https://github.com/doxygen/doxygen/releases/download/Release_${_doxygen_min}/doxygen-${doxygen_min}.src.tar.gz"
      echo "doxygen url: ${doxygen_url}"
      pushd "${build_dir}"
        wget "$doxygen_url" --progress=bar:force:noscroll -q --show-progress -O "doxygen.tar.gz"
        tar -xzf "doxygen.tar.gz"
        cd "doxygen-${doxygen_min}"
        cmake -DCMAKE_BUILD_TYPE=Release -G="Ninja" -B="build" -S="."
        ninja -C "build" -j"${num_processors}"
        ${sudo_cmd} ninja -C "build" install
      popd
    else
      echo "Doxygen version not in range, skipping docs"
      # Note: cmake_args will be set in cmake step
    fi
  fi

  # install node from nvm
  if [ "$nvm_node" == 1 ]; then
    nvm_url="https://raw.githubusercontent.com/nvm-sh/nvm/master/install.sh"
    echo "nvm url: ${nvm_url}"
    wget -qO- ${nvm_url} | bash

    # shellcheck source=/dev/null  # we don't care that shellcheck cannot find nvm.sh
    source "$HOME/.nvm/nvm.sh"
    nvm install node
    nvm use node
  fi

  # run the cuda install
  if [ "$skip_cuda" == 0 ]; then
    install_cuda
  fi
}

function run_step_cmake() {
  echo "Running step: CMake configure"

  # Setup NVM environment if needed (for web UI builds)
  setup_nvm_environment

  # Export GCC version for toolchain files (in case it wasn't set in deps step)
  if [ "$distro" == "debian" ] || [ "$distro" == "ubuntu" ]; then
    export LINUX_GCC_VERSION="$gcc_version"
  fi

  # Detect CUDA path using the reusable function
  nvcc_path=""
  if [ "$skip_cuda" == 0 ]; then
    nvcc_path=$(detect_nvcc_path)
  fi

  # prepare CMAKE args
  cmake_args=(
    "-B=build"
    "-G=Ninja"
    "-S=."
    "-DBUILD_WERROR=ON"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_INSTALL_PREFIX=/usr"
    "-DSUNSHINE_ASSETS_DIR=share/sunshine"
    "-DSUNSHINE_EXECUTABLE_PATH=/usr/bin/sunshine"
    "-DSUNSHINE_ENABLE_WAYLAND=ON"
    "-DSUNSHINE_ENABLE_X11=ON"
    "-DSUNSHINE_ENABLE_DRM=ON"
  )

  # Add cross-compilation settings if enabled
  if [ "$cross_compile" == 1 ] && [ -n "$target_tuple" ]; then
    # Use CMake toolchain file for cross-compilation
    toolchain_file="${script_dir}/../cmake/toolchains/${target_tuple}.cmake"
    if [ -f "$toolchain_file" ]; then
      cmake_args+=("-DCMAKE_TOOLCHAIN_FILE=${toolchain_file}")
      echo "Using toolchain file: $toolchain_file"
    else
      echo "Warning: Toolchain file not found: $toolchain_file"
      # Fallback to manual configuration
      cmake_args+=("-DCMAKE_C_COMPILER=${target_tuple}-gcc")
      cmake_args+=("-DCMAKE_CXX_COMPILER=${target_tuple}-g++")
    fi
  fi

  if [ "$appimage_build" == 1 ]; then
    cmake_args+=("-DSUNSHINE_BUILD_APPIMAGE=ON")
  fi

  # Publisher metadata
  if [ -n "$publisher_name" ]; then
    cmake_args+=("-DSUNSHINE_PUBLISHER_NAME='${publisher_name}'")
  fi
  if [ -n "$publisher_website" ]; then
    cmake_args+=("-DSUNSHINE_PUBLISHER_WEBSITE='${publisher_website}'")
  fi
  if [ -n "$publisher_issue_url" ]; then
    cmake_args+=("-DSUNSHINE_PUBLISHER_ISSUE_URL='${publisher_issue_url}'")
  fi

  # Handle doxygen docs flag
  if ! check_version "doxygen" "$doxygen_min" "$doxygen_max"; then
    if [ "${SUNSHINE_COMPILE_DOXYGEN}" != "true" ]; then
      cmake_args+=("-DBUILD_DOCS=OFF")
    fi
  fi

  # Handle CUDA
  if [ "$skip_cuda" == 0 ]; then
    cmake_args+=("-DSUNSHINE_ENABLE_CUDA=ON")
    if [ -n "$nvcc_path" ]; then
      cmake_args+=("-DCMAKE_CUDA_COMPILER:PATH=$nvcc_path")
    fi
  else
    cmake_args+=("-DSUNSHINE_ENABLE_CUDA=OFF")
  fi

  # Cmake stuff here
  mkdir -p "build"
  echo "cmake args:"
  echo "${cmake_args[@]}"
  cmake "${cmake_args[@]}"
}

function run_step_validation() {
  echo "Running step: Validation"

  # Run appstream validation, etc.
  appstreamcli validate "build/dev.lizardbyte.app.Sunshine.metainfo.xml"
  appstream-util validate "build/dev.lizardbyte.app.Sunshine.metainfo.xml"
  desktop-file-validate "build/dev.lizardbyte.app.Sunshine.desktop"
  if [ "$appimage_build" == 0 ]; then
    desktop-file-validate "build/dev.lizardbyte.app.Sunshine.terminal.desktop"
  fi
}

function run_step_build() {
  echo "Running step: Build"

  # Setup NVM environment if needed (for web UI builds)
  setup_nvm_environment

  # Build the project
  ninja -C "build"
}

function run_step_package() {
  echo "Running step: Package"

  # Create the package
  if [ "$skip_package" == 0 ]; then
    if [ "$distro" == "debian" ] || [ "$distro" == "ubuntu" ]; then
      cpack -G DEB --config ./build/CPackConfig.cmake
    elif [ "$distro" == "fedora" ]; then
      cpack -G RPM --config ./build/CPackConfig.cmake
    fi
  fi
}

function run_step_cleanup() {
  echo "Running step: Cleanup"

  if [ "$skip_cleanup" == 0 ]; then
    # Restore the original gcc alternatives
    if [ "$distro" == "debian" ] || [ "$distro" == "ubuntu" ]; then
      for file in "${gcc_alternative_files[@]}"; do
        if [ -e "/etc/alternatives/$file.bak" ]; then
          ${sudo_cmd} mv "/etc/alternatives/$file.bak" "/etc/alternatives/$file"
        else
          ${sudo_cmd} rm "/etc/alternatives/$file"
        fi
      done
    fi

    # restore the math-vector.h file
    if [ "$architecture" == "aarch64" ] && [ -n "$math_vector_file" ]; then
      ${sudo_cmd} mv -f "$math_vector_file.bak" "$math_vector_file"
    fi
  fi
}

function run_install() {
  case "$step" in
    deps)
      run_step_deps
      ;;
    cmake)
      run_step_cmake
      ;;
    validation)
      run_step_validation
      ;;
    build)
      run_step_build
      ;;
    package)
      run_step_package
      ;;
    cleanup)
      run_step_cleanup
      ;;
    all)
      run_step_deps
      run_step_cmake
      run_step_validation
      run_step_build
      run_step_package
      run_step_cleanup
      ;;
    *)
      echo "Invalid step: $step"
      echo "Valid steps are: deps, cmake, validation, build, package, cleanup, all"
      exit 1
      ;;
  esac
}

# Determine the OS and call the appropriate function
cat /etc/os-release

if grep -q "Arch Linux" /etc/os-release; then
  distro="arch"
  version=""
  package_update_command="${sudo_cmd} pacman -Syu --noconfirm"
  package_install_command="${sudo_cmd} pacman -Sy --needed"
  nvm_node=0
  gcc_version="14"
elif grep -q "Debian GNU/Linux 12 (bookworm)" /etc/os-release; then
  distro="debian"
  version="12"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  cuda_version="12.9.1"
  cuda_build="575.57.08"
  gcc_version="13"
  nvm_node=0
elif grep -q "Debian GNU/Linux 13 (trixie)" /etc/os-release; then
  distro="debian"
  version="13"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  cuda_version="12.9.1"
  cuda_build="575.57.08"
  gcc_version="14"
  nvm_node=0
elif grep -q "PLATFORM_ID=\"platform:f41\"" /etc/os-release; then
  distro="fedora"
  version="41"
  package_update_command="${sudo_cmd} dnf update -y"
  package_install_command="${sudo_cmd} dnf install -y"
  cuda_version="12.9.1"
  cuda_build="575.57.08"
  gcc_version="13"
  nvm_node=0
  dev_tools_group="development-tools"
elif grep -q "PLATFORM_ID=\"platform:f42\"" /etc/os-release; then
  distro="fedora"
  version="42"
  package_update_command="${sudo_cmd} dnf update -y"
  package_install_command="${sudo_cmd} dnf install -y"
  cuda_version="12.9.1"
  cuda_build="575.57.08"
  gcc_version="14"
  nvm_node=0
  dev_tools_group="development-tools"
elif grep -q "Ubuntu 22.04" /etc/os-release; then
  distro="ubuntu"
  version="22.04"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  cuda_version="12.9.1"
  cuda_build="575.57.08"
  gcc_version="13"
  nvm_node=1
elif grep -q "Ubuntu 24.04" /etc/os-release; then
  distro="ubuntu"
  version="24.04"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  cuda_version="12.9.1"
  cuda_build="575.57.08"
  gcc_version="14"
  nvm_node=1
elif grep -q "Ubuntu 25.04" /etc/os-release; then
  distro="ubuntu"
  version="25.04"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  cuda_version="12.9.1"
  cuda_build="575.57.08"
  gcc_version="14"
  nvm_node=0
else
  echo "Unsupported Distro or Version"
  exit 1
fi

# Override package commands if aptitude is requested for Debian/Ubuntu systems
if [ "$use_aptitude" == 1 ] && ([ "$distro" == "debian" ] || [ "$distro" == "ubuntu" ]); then
  echo "Using aptitude for package management"
  package_update_command="${sudo_cmd} aptitude update"
  package_install_command="${sudo_cmd} aptitude install -y"
fi

architecture=$(uname -m)

# Override architecture for cross-compilation
if [ "$cross_compile" == 1 ] && [ -n "$target_arch" ]; then
  case "$target_arch" in
    arm64)
      architecture="aarch64"
      ;;
    amd64)
      architecture="x86_64"
      ;;
    *)
      echo "Unknown target architecture: $target_arch"
      exit 1
      ;;
  esac
  echo "Cross-compiling for architecture: $architecture (from target_arch: $target_arch)"
fi

echo "Detected Distro: $distro"
echo "Detected Version: $version"
echo "Detected Architecture: $architecture"

if [ "$architecture" != "x86_64" ] && [ "$architecture" != "aarch64" ]; then
  echo "Unsupported Architecture"
  exit 1
fi

# get directory of this script
script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
build_dir="$script_dir/../build"
echo "Script Directory: $script_dir"
echo "Build Directory: $build_dir"
mkdir -p "$build_dir"

run_install
