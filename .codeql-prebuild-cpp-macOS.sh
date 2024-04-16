# install dependencies for C++ analysis
set -e

# install dependencies
brew install \
  boost \
  cmake \
  miniupnpc \
  node \
  opus \
  pkg-config

# build
mkdir -p build
cd build || exit 1
cmake -G "Unix Makefiles" ..
make -j"$(sysctl -n hw.logicalcpu)"

# skip autobuild
echo "skip_autobuild=true" >> "$GITHUB_OUTPUT"
