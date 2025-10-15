#!/bin/bash
# Sunshine Local Build Script for MSYS2 UCRT64

set -e  # Exit on error

echo "=== Sunshine Local Build Script ==="
echo ""

# Update MSYS2
echo "Step 1: Updating MSYS2..."
pacman -Sy --noconfirm

# Install dependencies
echo ""
echo "Step 2: Installing build dependencies..."
pacman -S --noconfirm --needed \
    mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-boost \
    mingw-w64-ucrt-x86_64-openssl \
    mingw-w64-ucrt-x86_64-opus \
    mingw-w64-ucrt-x86_64-curl \
    mingw-w64-ucrt-x86_64-miniupnpc \
    mingw-w64-ucrt-x86_64-doxygen \
    git \
    make

# Initialize submodules
echo ""
echo "Step 3: Initializing git submodules..."
git submodule update --init --recursive --depth 1 || true

# Create build directory
echo ""
echo "Step 4: Creating build directory..."
mkdir -p build
cd build

# Configure with CMake
echo ""
echo "Step 5: Configuring with CMake..."
cmake .. \
    -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="../install"

# Build
echo ""
echo "Step 6: Building Sunshine..."
cmake --build . --config Release -j$(nproc)

echo ""
echo "=== Build Complete! ==="
echo "Sunshine executable: $(pwd)/sunshine.exe"
echo ""
echo "To test it, run:"
echo "  ./sunshine.exe --help"

