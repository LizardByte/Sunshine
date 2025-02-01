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
brew install "${dependencies[@]}"

# build
mkdir -p build
cmake \
  -B build \
  -G Ninja \
  -S . \
  -DBOOST_USE_STATIC=OFF \
  -DBUILD_DOCS=OFF \
  -DBUILD_WERROR=ON
ninja -C build

# skip autobuild
echo "skip_autobuild=true" >> "$GITHUB_OUTPUT"
