# install dependencies for C++ analysis

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
mingw32-make -j"$(sysctl -n hw.logicalcpu)"

# skip autobuild
echo "skip_autobuild=true" >> "$GITHUB_OUTPUT"
