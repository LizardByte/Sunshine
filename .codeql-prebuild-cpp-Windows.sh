# install dependencies for C++ analysis
set -e

# update pacman
pacman --noconfirm -Suy

# install dependencies
pacman --noconfirm -S \
  base-devel \
  diffutils \
  gcc \
  git \
  make \
  mingw-w64-x86_64-binutils \
  mingw-w64-x86_64-boost \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-curl \
  mingw-w64-x86_64-miniupnpc \
  mingw-w64-x86_64-nlohmann-json \
  mingw-w64-x86_64-nodejs \
  mingw-w64-x86_64-onevpl \
  mingw-w64-x86_64-openssl \
  mingw-w64-x86_64-opus \
  mingw-w64-x86_64-rust \
  mingw-w64-x86_64-toolchain

# build
mkdir -p build
cd build || exit 1
cmake -G "MinGW Makefiles" ..
mingw32-make -j"$(nproc)"

# skip autobuild
echo "skip_autobuild=true" >> "$GITHUB_OUTPUT"
