#!/usr/bin/env bash
# Note: This script is not used by CI, and is only for manually building/signing the .app.
# Changes made to this script should also be made in ci-macos.yml.
set -euo pipefail

# Default value for arguments
num_processors=$(sysctl -n hw.ncpu)
publisher_name="LizardByte"
publisher_website="https://app.lizardbyte.dev"
publisher_issue_url="https://app.lizardbyte.dev/support"
step="all"
build_docs="OFF"
build_tests="ON"
build_type="Release"
sign_app="true"

# environment variables
# BUILD_VERSION should be empty or cmake will assume a CI build
BUILD_VERSION=""
BRANCH=$(git rev-parse --abbrev-ref HEAD)
COMMIT=$(git rev-parse --short HEAD)

export BUILD_VERSION
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
This script builds a macOS .app bundle packaged inside a .dmg.

If the environment variable CODESIGN_IDENTITY is set, the app will be signed.
This must be a "Developer ID" identity.

For others to be able to open the .dmg, it must be notarized. Create a keychain profile named
"notarytool-password" based on the instructions at
https://developer.apple.com/documentation/security/customizing-the-notarization-workflow?language=objc

Usage:
  $0 [options]

Options:
  -h, --help               Display this help message.
  --num-processors         The number of processors to use for compilation. Default: ${num_processors}.
  --publisher-name         The name of the publisher (not developer) of the application.
  --publisher-website      The URL of the publisher's website.
  --publisher-issue-url    The URL of the publisher's support site or issue tracker.
                           If you provide a modified version of Sunshine, we kindly request that you use your own url.
  --step=STEP              Which step(s) to run: deps, cmake, build, dmg, or all (default: all)
  --debug                  Build in debug mode.
  --build-docs             Build docs.
  --skip-tests             Don't build the test suite.
  --skip-codesign          Don't sign/notarize the bundle.

Steps:
  deps                     Install dependencies only
  cmake                    Run cmake configure only
  build                    Build the project only
  dmg                      Create a DMG package
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
    "-S=."
    "-DBUILD_DOCS=${build_docs}"
    "-DBUILD_TESTS=${build_tests}"
    "-DBUILD_WERROR=ON"
    "-DCMAKE_BUILD_TYPE=${build_type}"
    "-DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3 2>/dev/null)"
    "-DOpus_ROOT_DIR=$(brew --prefix opus 2>/dev/null)"
    "-DSUNSHINE_BUILD_HOMEBREW=OFF"
    "-DSUNSHINE_ENABLE_TRAY=ON"
  )

  if [[ -n "${sign_app}" ]]; then
    if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
      cmake_args+=("-DCODESIGN_IDENTITY='${CODESIGN_IDENTITY}'")
    else
      echo "Please set the CODESIGN_IDENTITY environment variable or use --skip-codesign"
      exit 1
    fi
  fi

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
  cmake --build "${build_dir}" -j "${num_processors}"
  return 0
}

function run_step_dmg() {
  echo "Running step: Creating DMG package"

  # This variable is needed by cmake/packaging/macos.cmake
  SHOULD_SIGN=0
  if [[ -n "${sign_app}" ]]; then
    SHOULD_SIGN=1
  fi
  export SHOULD_SIGN

  cpack -G DragNDrop --config "${build_dir}/CPackConfig.cmake" --verbose

  if [[ -n "${sign_app}" ]]; then
    xcrun notarytool submit "${build_dir}/cpack_artifacts/Sunshine.dmg" --keychain-profile "notarytool-password" --wait
    xcrun stapler staple -v "${build_dir}/cpack_artifacts/Sunshine.dmg"
  fi
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
    dmg)
      run_step_dmg
      ;;
    all)
      run_step_cmake
      run_step_build
      run_step_dmg
      ;;
    *)
      echo "Invalid step: $step"
      echo "Valid steps are: deps, cmake, build, dmg, all"
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
        build-docs)
          build_docs="ON"
          ;;
        skip-tests)
          build_tests="OFF"
          ;;
        skip-codesign)
         sign_app=""
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
