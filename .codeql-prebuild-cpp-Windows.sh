# install dependencies for C++ analysis
set -e

# update pacman
pacman --noconfirm -Suy

# install wget
pacman --noconfirm -S \
  wget

# download working curl
wget https://repo.msys2.org/mingw/ucrt64/mingw-w64-ucrt-x86_64-curl-8.8.0-1-any.pkg.tar.zst

# install dependencies
pacman -U --noconfirm mingw-w64-ucrt-x86_64-curl-8.8.0-1-any.pkg.tar.zst
pacman -Syu --noconfirm --ignore=mingw-w64-ucrt-x86_64-curl \
  base-devel \
  diffutils \
  gcc \
  git \
  make \
  mingw-w64-ucrt-x86_64-boost \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-cppwinrt \
  mingw-w64-ucrt-x86_64-graphviz \
  mingw-w64-ucrt-x86_64-miniupnpc \
  mingw-w64-ucrt-x86_64-nlohmann-json \
  mingw-w64-ucrt-x86_64-nodejs \
  mingw-w64-ucrt-x86_64-nsis \
  mingw-w64-ucrt-x86_64-onevpl \
  mingw-w64-ucrt-x86_64-openssl \
  mingw-w64-ucrt-x86_64-opus \
  mingw-w64-ucrt-x86_64-rust \
  mingw-w64-ucrt-x86_64-toolchain

# build
mkdir -p build
cd build || exit 1
cmake \
  -DBUILD_DOCS=OFF \
  -G "MinGW Makefiles" ..
mingw32-make -j"$(nproc)"

# skip autobuild
echo "skip_autobuild=true" >> "$GITHUB_OUTPUT"
