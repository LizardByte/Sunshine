# Building Sunshine for Windows ARM64

This guide covers cross-compiling Sunshine for Windows ARM64 (Qualcomm Snapdragon) from a Linux host using the llvm-mingw toolchain.

## Prerequisites

- Ubuntu 22.04+ or WSL2
- ~10GB disk space
- Internet connection for downloading dependencies

## Quick Start

```bash
# Install build tools
sudo apt update
sudo apt install -y cmake ninja-build git curl wget pkg-config \
    python3 nodejs npm nasm

# Download llvm-mingw toolchain
cd ~
wget https://github.com/mstorsjo/llvm-mingw/releases/download/20241119/llvm-mingw-20241119-ucrt-ubuntu-22.04-x86_64.tar.xz
tar xf llvm-mingw-20241119-ucrt-ubuntu-22.04-x86_64.tar.xz
mv llvm-mingw-20241119-ucrt-ubuntu-22.04-x86_64 toolchains/llvm-mingw

# Clone Sunshine
git clone --recursive https://github.com/LizardByte/Sunshine.git
cd Sunshine
```

## Building Dependencies

The following dependencies must be built for ARM64. Create a working directory:

```bash
mkdir -p ~/deps-arm64/install
cd ~/deps-arm64
export TC=~/toolchains/llvm-mingw
export TARGET=aarch64-w64-mingw32
export PREFIX=~/deps-arm64/install
```

### OpenSSL 3.4.0

```bash
wget https://github.com/openssl/openssl/releases/download/openssl-3.4.0/openssl-3.4.0.tar.gz
tar xf openssl-3.4.0.tar.gz
cd openssl-3.4.0

./Configure mingw64 --cross-compile-prefix=$TC/bin/$TARGET- \
    --prefix=$PREFIX --libdir=lib no-shared no-tests
make -j$(nproc)
make install_sw
cd ..
```

### libcurl 8.11.1

```bash
wget https://curl.se/download/curl-8.11.1.tar.xz
tar xf curl-8.11.1.tar.xz
cd curl-8.11.1
mkdir build && cd build

cmake .. -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=~/Sunshine/cmake/toolchains/aarch64-w64-mingw32.cmake \
    -DCMAKE_PREFIX_PATH=$PREFIX \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DBUILD_SHARED_LIBS=OFF \
    -DCURL_USE_OPENSSL=ON \
    -DCURL_DISABLE_LDAP=ON \
    -DBUILD_CURL_EXE=OFF \
    -DBUILD_TESTING=OFF

ninja && ninja install
cd ../..
```

### miniupnpc 2.2.8

```bash
wget https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_2_8.tar.gz
tar xf miniupnpc_2_2_8.tar.gz
cd miniupnp-miniupnpc_2_2_8/miniupnpc
mkdir build && cd build

cmake .. -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=~/Sunshine/cmake/toolchains/aarch64-w64-mingw32.cmake \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DUPNPC_BUILD_STATIC=ON \
    -DUPNPC_BUILD_SHARED=OFF \
    -DUPNPC_BUILD_TESTS=OFF \
    -DUPNPC_BUILD_SAMPLE=OFF

ninja && ninja install
cd ../../..
```

### Opus 1.5.2

```bash
wget https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz
tar xf opus-1.5.2.tar.gz
cd opus-1.5.2
mkdir build && cd build

cmake .. -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=~/Sunshine/cmake/toolchains/aarch64-w64-mingw32.cmake \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DBUILD_SHARED_LIBS=OFF \
    -DOPUS_BUILD_TESTING=OFF \
    -DOPUS_BUILD_PROGRAMS=OFF

ninja && ninja install
cd ../..
```

### Stub Libraries

Some libraries are not available on ARM64 and require stubs:

#### MinHook (not available on ARM64)

```bash
mkdir -p $PREFIX/lib
cat > /tmp/minhook_stub.c << 'EOF'
int MH_Initialize(void) { return 0; }
int MH_Uninitialize(void) { return 0; }
int MH_CreateHook(void *a, void *b, void **c) { return 0; }
int MH_EnableHook(void *a) { return 0; }
int MH_DisableHook(void *a) { return 0; }
EOF
$TC/bin/$TARGET-clang -c /tmp/minhook_stub.c -o /tmp/minhook_stub.o
$TC/bin/$TARGET-ar rcs $PREFIX/lib/libMinHook.a /tmp/minhook_stub.o
```

#### VPL (Intel QuickSync - not available on ARM64)

```bash
cat > /tmp/vpl_stub.c << 'EOF'
int MFXInit(int a, void *b, void **c) { return -1; }
int MFXClose(void *a) { return 0; }
int MFXQueryVersion(void *a, void *b) { return -1; }
int MFXInitEx(void *a, void **b) { return -1; }
EOF
$TC/bin/$TARGET-clang -c /tmp/vpl_stub.c -o /tmp/vpl_stub.o
$TC/bin/$TARGET-ar rcs $PREFIX/lib/libvpl.a /tmp/vpl_stub.o

mkdir -p $PREFIX/include/vpl
cat > $PREFIX/include/vpl/mfxvideo.h << 'EOF'
#pragma once
typedef int mfxStatus;
typedef void* mfxSession;
EOF
```

## CMake Toolchain File

Create `cmake/toolchains/aarch64-w64-mingw32.cmake`:

```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(TOOLCHAIN_ROOT "$ENV{HOME}/toolchains/llvm-mingw")
set(TARGET_TRIPLE "aarch64-w64-mingw32")

set(CMAKE_C_COMPILER "${TOOLCHAIN_ROOT}/bin/${TARGET_TRIPLE}-clang")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/${TARGET_TRIPLE}-clang++")
set(CMAKE_RC_COMPILER "${TOOLCHAIN_ROOT}/bin/${TARGET_TRIPLE}-windres")
set(CMAKE_AR "${TOOLCHAIN_ROOT}/bin/${TARGET_TRIPLE}-ar")
set(CMAKE_RANLIB "${TOOLCHAIN_ROOT}/bin/${TARGET_TRIPLE}-ranlib")

set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_ROOT}/${TARGET_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++ -D_WIN32_WINNT=0x0A00")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -lc++ -lc++abi")
```

## Building Sunshine

```bash
cd ~/Sunshine
mkdir build-arm64 && cd build-arm64

cmake .. -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchains/aarch64-w64-mingw32.cmake \
    -DCMAKE_PREFIX_PATH="$PREFIX;$HOME/Sunshine/third-party/build-deps/dist/Windows-ARM64" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSUNSHINE_ENABLE_TRAY=OFF \
    -DSUNSHINE_ENABLE_DRM=OFF \
    -DSUNSHINE_ENABLE_WAYLAND=OFF \
    -DSUNSHINE_ENABLE_X11=OFF \
    -DSUNSHINE_ENABLE_CUDA=OFF \
    -DSUNSHINE_BUILD_APPIMAGE=OFF \
    -DSUNSHINE_PUBLISHER_NAME="LizardByte"

ninja sunshine.exe
```

## Running on Windows ARM64

1. Copy the built files to your Windows ARM64 device:
   - `sunshine.exe`
   - `assets/` folder (from `src_assets/windows/assets/`)

2. Install ViGEmBus driver for controller support

3. Run `sunshine.exe` from an elevated command prompt

## Hardware Encoding

Sunshine will automatically detect and use the Qualcomm Adreno GPU for hardware encoding via Windows Media Foundation:

- **h264_mf**: H.264 hardware encoding
- **hevc_mf**: HEVC hardware encoding
- **av1_mf**: AV1 hardware encoding (if supported)

### Known Limitations

- Only SDR 4:2:0 8-bit encoding supported (Qualcomm MF limitation)
- No HDR or YUV444 support
- ~8ms encoder latency overhead compared to direct GPU APIs
- Software encoding (libx264) may perform better in some cases

To use software encoding, set `encoder = software` in `sunshine.conf`.

## Troubleshooting

### "Encoder did not produce IDR frame"
This is normal for the first frame with Media Foundation encoders. The encoder uses a fixed GOP size of 120 frames.

### Large encoded frames / FEC warnings
The Media Foundation encoder may produce larger keyframes. This is handled automatically but may cause occasional FEC skip warnings.

### Shader compilation errors
Ensure the `assets/shaders/` directory is present alongside `sunshine.exe`.

### No GPU detected
Verify the Qualcomm Adreno driver is installed. Check Device Manager for the GPU.
