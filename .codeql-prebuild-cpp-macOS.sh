# install dependencies for C++ analysis
set -e

# install dependencies
dependencies=(
  "boost"
  "cmake"
  "miniupnpc"
  "ninja"
  "node"
  "openssl@3"
  "opus"
  "pkg-config"
)
arch -arm64 brew install "${dependencies[@]}"

# build
mkdir -p build
cmake \
  -B build \
  -G Ninja \
  -S . \
  -DBOOST_USE_STATIC=OFF \
  -DBUILD_DOCS=OFF \
  -DBUILD_WERROR=ON \
  -DCMAKE_TOOLCHAIN_FILE="./cmake/toolchains/Darwin-arm64.cmake"
ninja -C build

# skip autobuild
echo "skip_autobuild=true" >> "$GITHUB_OUTPUT"
