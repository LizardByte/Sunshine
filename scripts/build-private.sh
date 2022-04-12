#!/bin/bash -e
set -e

CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
SUNSHINE_EXECUTABLE_PATH="${SUNSHINE_EXECUTABLE_PATH:-/usr/bin/sunshine}"
SUNSHINE_ASSETS_DIR="${SUNSHINE_ASSETS_DIR:-/etc/sunshine}"


SUNSHINE_ROOT="${SUNSHINE_ROOT:-/root/sunshine}"
SUNSHINE_TAG="${SUNSHINE_TAG:-master}"
SUNSHINE_GIT_URL="${SUNSHINE_GIT_URL:-https://github.com/sunshinestream/sunshine.git}"


SUNSHINE_ENABLE_WAYLAND=${SUNSHINE_ENABLE_WAYLAND:-ON}
SUNSHINE_ENABLE_X11=${SUNSHINE_ENABLE_X11:-ON}
SUNSHINE_ENABLE_DRM=${SUNSHINE_ENABLE_DRM:-ON}
SUNSHINE_ENABLE_CUDA=${SUNSHINE_ENABLE_CUDA:-ON}

# For debugging, it would be usefull to have the sources on the host.
if [[ ! -d "$SUNSHINE_ROOT" ]]
then
    git clone --depth 1 --branch "$SUNSHINE_TAG" "$SUNSHINE_GIT_URL" --recurse-submodules "$SUNSHINE_ROOT"
fi

if [[ ! -d /root/sunshine-build ]]
then
	mkdir -p /root/sunshine-build
fi
cd /root/sunshine-build

cmake "-DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE" "-DSUNSHINE_EXECUTABLE_PATH=$SUNSHINE_EXECUTABLE_PATH" "-DSUNSHINE_ASSETS_DIR=$SUNSHINE_ASSETS_DIR" "-DSUNSHINE_ENABLE_WAYLAND=$SUNSHINE_ENABLE_WAYLAND" "-DSUNSHINE_ENABLE_X11=$SUNSHINE_ENABLE_X11" "-DSUNSHINE_ENABLE_DRM=$SUNSHINE_ENABLE_DRM" "-DSUNSHINE_ENABLE_CUDA=$SUNSHINE_ENABLE_CUDA" "$SUNSHINE_ROOT"

make -j ${nproc}

# Get preferred package format
if [ "$1" == "-rpm" ]
then
  echo "Packaging in .rpm format."
  ./gen-rpm -d
elif [ "$1" == "-deb" ]
then
  echo "Packaging in .deb format."
  ./gen-deb
else
  echo "Preferred packaging not specified."
  echo "Use -deb or -rpm to specify preferred package format."
  exit 1
fi
