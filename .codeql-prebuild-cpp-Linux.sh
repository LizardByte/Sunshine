# install dependencies for C++ analysis
set -e

chmod +x ./scripts/linux_build.sh
./scripts/linux_build.sh --skip-package --ubuntu-test-repo

# Delete CUDA
rm -rf ./build/cuda

# skip autobuild
echo "skip_autobuild=true" >> "$GITHUB_OUTPUT"
