# install dependencies for C++ analysis
set -e

# setup homebrew for x86_64
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
eval "$(/usr/local/bin/brew shellenv)"

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
