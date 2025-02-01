#!/bin/bash
set -e

# Default value for arguments
appimage_build=0
publisher_name="Third Party Publisher"
publisher_website=""
publisher_issue_url="https://app.lizardbyte.dev/support"
skip_cleanup=0
skip_cuda=0
skip_libva=0
skip_package=0
sudo_cmd="sudo"
ubuntu_test_repo=0

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
  --publisher-name         The name of the publisher (not developer) of the application.
  --publisher-website      The URL of the publisher's website.
  --publisher-issue-url    The URL of the publisher's support site or issue tracker.
                           If you provide a modified version of Sunshine, we kindly request that you use your own url.
  --skip-cleanup           Do not restore the original gcc alternatives, or the math-vector.h file.
  --skip-cuda              Skip CUDA installation.
  --skip-libva             Skip libva installation. This will automatically be enabled if passing --appimage-build.
  --skip-package           Skip creating DEB, or RPM package.
  --ubuntu-test-repo       Install ppa:ubuntu-toolchain-r/test repo on Ubuntu.
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
        ubuntu-test-repo) ubuntu_test_repo=1 ;;
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

function add_debain_based_deps() {
  dependencies+=(
    "bison"  # required if we need to compile doxygen
    "build-essential"
    "cmake"
    "doxygen"
    "flex"  # required if we need to compile doxygen
    "gcc-${gcc_version}"
    "g++-${gcc_version}"
    "git"
    "graphviz"
    "libcap-dev"  # KMS
    "libcurl4-openssl-dev"
    "libdrm-dev"  # KMS
    "libevdev-dev"
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
    "ninja-build"
    "npm"  # web-ui
    "udev"
    "wget"  # necessary for cuda install with `run` file
    "xvfb"  # necessary for headless unit testing
  )

  if [ "$skip_libva" == 0 ]; then
    dependencies+=(
      "libva-dev"  # VA-API
    )
  fi
}

function add_debain_deps() {
  add_debain_based_deps
  dependencies+=(
    "libayatana-appindicator3-dev"
  )
}

function add_ubuntu_deps() {
  if [ "$ubuntu_test_repo" == 1 ]; then
    # allow newer gcc
    ${sudo_cmd} add-apt-repository ppa:ubuntu-toolchain-r/test -y
  fi

  add_debain_based_deps
  dependencies+=(
    "libappindicator3-dev"
  )
}

function add_fedora_deps() {
  dependencies+=(
    "cmake"
    "doxygen"
    "gcc"
    "g++"
    "git"
    "graphviz"
    "libappindicator-gtk3-devel"
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
  # check if we need to install cuda
  if [ -f "${build_dir}/cuda/bin/nvcc" ]; then
    echo "cuda already installed"
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
}

function check_version() {
  local package_name=$1
  local min_version=$2
  local installed_version

  echo "Checking if $package_name is installed and at least version $min_version"

  if [ "$distro" == "debian" ] || [ "$distro" == "ubuntu" ]; then
    installed_version=$(dpkg -s "$package_name" 2>/dev/null | grep '^Version:' | awk '{print $2}')
  elif [ "$distro" == "fedora" ]; then
    installed_version=$(rpm -q --queryformat '%{VERSION}' "$package_name" 2>/dev/null)
  else
    echo "Unsupported Distro"
    return 1
  fi

  if [ -z "$installed_version" ]; then
    echo "Package not installed"
    return 1
  fi

  if [ "$(printf '%s\n' "$installed_version" "$min_version" | sort -V | head -n1)" = "$min_version" ]; then
    echo "$package_name version $installed_version is at least $min_version"
    return 0
  else
    echo "$package_name version $installed_version is less than $min_version"
    return 1
  fi
}

function run_install() {
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

  # Update the package list
  $package_update_command

  if [ "$distro" == "debian" ]; then
    add_debain_deps
  elif [ "$distro" == "ubuntu" ]; then
    add_ubuntu_deps
  elif [ "$distro" == "fedora" ]; then
    add_fedora_deps
    ${sudo_cmd} dnf group install "Development Tools" -y
  fi

  # Install the dependencies
  $package_install_command "${dependencies[@]}"

  # reload the environment
  # shellcheck source=/dev/null
  source ~/.bashrc

  gcc_alternative_files=(
    "gcc"
    "g++"
    "gcov"
    "gcc-ar"
    "gcc-ranlib"
  )

  # update alternatives for gcc and g++ if a debian based distro
  if [ "$distro" == "debian" ] || [ "$distro" == "ubuntu" ]; then
    for file in "${gcc_alternative_files[@]}"; do
      file_path="/etc/alternatives/$file"
      if [ -e "$file_path" ]; then
        mv "$file_path" "$file_path.bak"
      fi
    done

    ${sudo_cmd} update-alternatives --install \
      /usr/bin/gcc gcc /usr/bin/gcc-${gcc_version} 100 \
      --slave /usr/bin/g++ g++ /usr/bin/g++-${gcc_version} \
      --slave /usr/bin/gcov gcov /usr/bin/gcov-${gcc_version} \
      --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-${gcc_version} \
      --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-${gcc_version}
  fi

  # compile cmake if the version is too low
  cmake_min="3.25.0"
  target_cmake_version="3.30.1"
  if ! check_version "cmake" "$cmake_min"; then
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
  doxygen_min="1.10.0"
  _doxygen_min="1_10_0"
  if ! check_version "doxygen" "$doxygen_min"; then
    if [ "${SUNSHINE_COMPILE_DOXYGEN}" == "true" ]; then
      echo "Compiling doxygen"
      doxygen_url="https://github.com/doxygen/doxygen/releases/download/Release_${_doxygen_min}/doxygen-${doxygen_min}.src.tar.gz"
      echo "doxygen url: ${doxygen_url}"
      wget "$doxygen_url" --progress=bar:force:noscroll -q --show-progress -O "${build_dir}/doxygen.tar.gz"
      tar -xzf "${build_dir}/doxygen.tar.gz"
      cd "doxygen-${doxygen_min}"
      cmake -DCMAKE_BUILD_TYPE=Release -G="Ninja" -B="build" -S="."
      ninja -C "build"
      ninja -C "build" install
    else
      echo "Doxygen version too low, skipping docs"
      cmake_args+=("-DBUILD_DOCS=OFF")
    fi
  fi

  # install node from nvm
  if [ "$nvm_node" == 1 ]; then
    nvm_url="https://raw.githubusercontent.com/nvm-sh/nvm/master/install.sh"
    echo "nvm url: ${nvm_url}"
    wget -qO- ${nvm_url} | bash
    source "$HOME/.nvm/nvm.sh"
    nvm install node
    nvm use node
  fi

  # run the cuda install
  if [ -n "$cuda_version" ] && [ "$skip_cuda" == 0 ]; then
    install_cuda
    cmake_args+=("-DSUNSHINE_ENABLE_CUDA=ON")
    cmake_args+=("-DCMAKE_CUDA_COMPILER:PATH=${build_dir}/cuda/bin/nvcc")
  fi

  # Cmake stuff here
  mkdir -p "build"
  echo "cmake args:"
  echo "${cmake_args[@]}"
  cmake "${cmake_args[@]}"
  ninja -C "build"

  # Create the package
  if [ "$skip_package" == 0 ]; then
    if [ "$distro" == "debian" ] || [ "$distro" == "ubuntu" ]; then
      cpack -G DEB --config ./build/CPackConfig.cmake
    elif [ "$distro" == "fedora" ]; then
      cpack -G RPM --config ./build/CPackConfig.cmake
    fi
  fi

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

# Determine the OS and call the appropriate function
cat /etc/os-release
if grep -q "Debian GNU/Linux 12 (bookworm)" /etc/os-release; then
  distro="debian"
  version="12"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  cuda_version="12.0.0"
  cuda_build="525.60.13"
  gcc_version="12"
  nvm_node=0
elif grep -q "PLATFORM_ID=\"platform:f39\"" /etc/os-release; then
  distro="fedora"
  version="39"
  package_update_command="${sudo_cmd} dnf update -y"
  package_install_command="${sudo_cmd} dnf install -y"
  cuda_version="12.4.0"
  cuda_build="550.54.14"
  gcc_version="13"
  nvm_node=0
elif grep -q "PLATFORM_ID=\"platform:f40\"" /etc/os-release; then
  distro="fedora"
  version="40"
  package_update_command="${sudo_cmd} dnf update -y"
  package_install_command="${sudo_cmd} dnf install -y"
  cuda_version=
  cuda_build=
  gcc_version="13"
  nvm_node=0
elif grep -q "Ubuntu 22.04" /etc/os-release; then
  distro="ubuntu"
  version="22.04"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  cuda_version="11.8.0"
  cuda_build="520.61.05"
  gcc_version="11"
  nvm_node=1
elif grep -q "Ubuntu 24.04" /etc/os-release; then
  distro="ubuntu"
  version="24.04"
  package_update_command="${sudo_cmd} apt-get update"
  package_install_command="${sudo_cmd} apt-get install -y"
  cuda_version="11.8.0"
  cuda_build="520.61.05"
  gcc_version="11"
  nvm_node=0
else
  echo "Unsupported Distro or Version"
  exit 1
fi

architecture=$(uname -m)

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
