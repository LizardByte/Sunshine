# install dependencies for C++ analysis
set -e

# install dependencies
dependencies=(
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
cd build || exit 1
cmake \
  -DBOOST_USE_STATIC=OFF \
  -DBUILD_DOCS=OFF \
  -G "Unix Makefiles" ..
make -j"$(sysctl -n hw.logicalcpu)"

# skip autobuild
echo "skip_autobuild=true" >> "$GITHUB_OUTPUT"
