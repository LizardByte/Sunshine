#!/usr/bin/env bash
set -euo pipefail

# Default value for arguments
num_processors=$(sysctl -n hw.ncpu)
publisher_name="LizardByte"
publisher_website="https://app.lizardbyte.dev"
publisher_issue_url="https://app.lizardbyte.dev/support"
step="all"
build_docs="ON"
build_type="Release"
build_system="Unix Makefiles"

# environment variables
BUILD_VER=""
BRANCH=$(git rev-parse --abbrev-ref HEAD)
COMMIT=$(git rev-parse --short HEAD)

export BUILD_VER
export BRANCH
export COMMIT

# boost could be included here but cmake will build the right version we need
required_formulas=(
  "cmake"
  "doxygen"
  "graphviz"
  "node"
  "pkgconf"
  "icu4c@78"
  "miniupnpc"
  "openssl@3"
  "opus"
  "llvm"
)

function _usage() {
  local exit_code=$1

  cat <<EOF
This script installs the dependencies and builds the project.
The script is intended to be run on an Apple Silicon Mac,
but may work on Intel as well.

Usage:
  $0 [options]

Options:
  -h, --help               Display this help message.
  --num-processors         The number of processors to use for compilation. Default: ${num_processors}.
  --publisher-name         The name of the publisher (not developer) of the application.
  --publisher-website      The URL of the publisher's website.
  --publisher-issue-url    The URL of the publisher's support site or issue tracker.
                           If you provide a modified version of Sunshine, we kindly request that you use your own url.
  --step                   Which step(s) to run: deps, cmake, build, or all (default: all)
  --debug                  Build in debug mode.
  --skip-docs              Don't build docs.

Steps:
  deps                     Install dependencies only
  cmake                    Run cmake configure only
  build                    Build the project only
  all                      Run all steps (default)
EOF

  exit "$exit_code"
}

function run_step_deps() {
  echo "Running step: Install dependencies"
  brew update
  brew install "${required_formulas[@]}"
  return 0
}

function run_step_cmake() {
  echo "Running step: CMake configure"

  # prepare CMAKE args
  cmake_args=(
    "-B=build"
    "-G=${build_system}"
    "-S=."
    "-DCMAKE_BUILD_TYPE=${build_type}"
    "-DBUILD_WERROR=ON"
    "-DHOMEBREW_ALLOW_FETCHCONTENT=ON"
    "-DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3 2>/dev/null)"
    "-DSUNSHINE_ASSETS_DIR=sunshine/assets"
    "-DSUNSHINE_BUILD_HOMEBREW=ON"
    "-DSUNSHINE_ENABLE_TRAY=ON"
    "-DBUILD_DOCS=${build_docs}"
    "-DBOOST_USE_STATIC=OFF"
  )

  # Publisher metadata
  if [[ -n "$publisher_name" ]]; then
    cmake_args+=("-DSUNSHINE_PUBLISHER_NAME='${publisher_name}'")
  fi
  if [[ -n "$publisher_website" ]]; then
    cmake_args+=("-DSUNSHINE_PUBLISHER_WEBSITE='${publisher_website}'")
  fi
  if [[ -n "$publisher_issue_url" ]]; then
    cmake_args+=("-DSUNSHINE_PUBLISHER_ISSUE_URL='${publisher_issue_url}'")
  fi

  # Cmake stuff here
  mkdir -p "build"
  echo "cmake args:"
  echo "${cmake_args[@]}"
  cmake "${cmake_args[@]}"
  return 0
}

function run_step_build() {
  echo "Running step: Build"
  make -C "${build_dir}" -j "${num_processors}"

  echo "*** To complete installation, run:"
  echo
  echo "  sudo make -C \"${build_dir}\" install"
  echo "  /usr/local/bin/sunshine"
  return 0
}

function run_install() {
  case "$step" in
    deps)
      run_step_deps
      ;;
    cmake)
      run_step_cmake
      ;;
    build)
      run_step_build
      ;;
    all)
      run_step_deps
      run_step_cmake
      run_step_build
      ;;
    *)
      echo "Invalid step: $step"
      echo "Valid steps are: deps, cmake, build, all"
      exit 1
      ;;
  esac
  return 0
}

# Parse named arguments
while getopts ":h-:" opt; do
  case ${opt} in
    h ) _usage 0 ;;
    - )
      case "${OPTARG}" in
        help) _usage 0 ;;
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
        step=*)
          step="${OPTARG#*=}"
          ;;
        debug)
          build_type="Debug"
          ;;
        skip-docs)
          build_docs="OFF"
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

# get directory of this script
script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
build_dir="$script_dir/../build"
echo "Script Directory: $script_dir"
echo "Build Directory: $build_dir"
mkdir -p "$build_dir"

run_install
