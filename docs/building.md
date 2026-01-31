# Building
Sunshine binaries are built using [CMake](https://cmake.org) and requires `cmake` > 3.25.

## Building Locally

### Compiler
It is recommended to use one of the following compilers:

| Compiler    | Version |
|:------------|:--------|
| GCC         | 13+     |
| Clang       | 17+     |
| Apple Clang | 15+     |

### Dependencies

#### FreeBSD
> [!CAUTION]
> Sunshine support for FreeBSD is experimental and may be incomplete or not work as expected

##### Install dependencies
```sh
pkg install -y \
  audio/opus \
  audio/pulseaudio \
  devel/cmake \
  devel/evdev-proto \
  devel/git \
  devel/libayatana-appindicator \
  devel/libevdev \
  devel/libnotify \
  devel/ninja \
  devel/pkgconf \
  ftp/curl \
  graphics/libdrm \
  graphics/wayland \
  multimedia/libva \
  net/miniupnpc \
  ports-mgmt/pkg \
  security/openssl \
  shells/bash \
  www/npm \
  x11/libX11 \
  x11/libxcb \
  x11/libXfixes \
  x11/libXrandr \
  x11/libXtst
```

#### Linux
Dependencies vary depending on the distribution. You can reference our
[linux_build.sh](https://github.com/LizardByte/Sunshine/blob/master/scripts/linux_build.sh) script for a list of
dependencies we use in Debian-based, Fedora-based and Arch-based distributions. Please submit a PR if you would like to extend the
script to support other distributions.

##### KMS Capture
If you are using KMS, patching the Sunshine binary with `setcap` is required. Some post-install scripts handle this. If building
from source and using the binary directly, this will also work:

```bash
sudo cp build/sunshine /tmp
sudo setcap cap_sys_admin+p /tmp/sunshine
sudo getcap /tmp/sunshine
sudo mv /tmp/sunshine build/sunshine
```

##### CUDA Toolkit
Sunshine requires CUDA Toolkit for NVFBC capture. There are two caveats to CUDA:

1. The version installed depends on the version of GCC.
2. The version of CUDA you use will determine compatibility with various GPU generations.
   At the time of writing, the recommended version to use is CUDA ~12.9.
   See [CUDA compatibility](https://docs.nvidia.com/deploy/cuda-compatibility/index.html) for more info.

> [!NOTE]
> To install older versions, select the appropriate run file based on your desired CUDA version and architecture
> according to [CUDA Toolkit Archive](https://developer.nvidia.com/cuda-toolkit-archive)

#### macOS
You can either use [Homebrew](https://brew.sh) or [MacPorts](https://www.macports.org) to install dependencies.

##### Homebrew
```bash
dependencies=(
  "boost"  # Optional
  "cmake"
  "doxygen"  # Optional, for docs
  "graphviz"  # Optional, for docs
  "icu4c"  # Optional, if boost is not installed
  "miniupnpc"
  "ninja"
  "node"
  "openssl@3"
  "opus"
  "pkg-config"
)
brew install "${dependencies[@]}"
```

If there are issues with an SSL header that is not found:

@tabs{
  @tab{ Intel | ```bash
    ln -s /usr/local/opt/openssl/include/openssl /usr/local/include/openssl
    ```}
  @tab{ Apple Silicon | ```bash
    ln -s /opt/homebrew/opt/openssl/include/openssl /opt/homebrew/include/openssl
    ```
  }
}

##### MacPorts
```bash
dependencies=(
  "cmake"
  "curl"
  "doxygen"  # Optional, for docs
  "graphviz"  # Optional, for docs
  "libopus"
  "miniupnpc"
  "ninja"
  "npm9"
  "pkgconfig"
)
sudo port install "${dependencies[@]}"
```

#### Windows

@warning{Cross-compilation is not supported on Windows. You must build on the target architecture.}

First you need to install [MSYS2](https://www.msys2.org).
 
For AMD64 startup "MSYS2 UCRT64", or for ARM64 startup "MSYS2 CLANGARM64", then execute the following commands.

##### Update all packages
```bash
pacman -Syu
```

##### Set toolchain variable
For UCRT64:
```bash
export TOOLCHAIN="ucrt-x86_64"
```

For CLANGARM64:
```bash
export TOOLCHAIN="clang-aarch64"
```

##### Install dependencies
```bash
dependencies=(
  "git"
  "mingw-w64-${TOOLCHAIN}-boost"  # Optional
  "mingw-w64-${TOOLCHAIN}-cmake"
  "mingw-w64-${TOOLCHAIN}-cppwinrt"
  "mingw-w64-${TOOLCHAIN}-curl-winssl"
  "mingw-w64-${TOOLCHAIN}-graphviz"  # Optional, for docs
  "mingw-w64-${TOOLCHAIN}-miniupnpc"
  "mingw-w64-${TOOLCHAIN}-nodejs"
  "mingw-w64-${TOOLCHAIN}-onevpl"
  "mingw-w64-${TOOLCHAIN}-openssl"
  "mingw-w64-${TOOLCHAIN}-opus"
  "mingw-w64-${TOOLCHAIN}-toolchain"
)
if [[ ${MSYSTEM} == "ucrt64" ]]; then
  dependencies+=(
    "mingw-w64-${TOOLCHAIN}-MinHook"
    "mingw-w64-${TOOLCHAIN}-nsis"  # TODO: how to create an arm64 installer?
  )
fi
pacman -S "${dependencies[@]}"
```

### Clone
Ensure [git](https://git-scm.com) is installed on your system, then clone the repository using the following command:

```bash
git clone https://github.com/lizardbyte/sunshine.git --recurse-submodules
cd sunshine
mkdir build
```

### Build

```bash
cmake -B build -G Ninja -S .
ninja -C build
```

> [!TIP]
> Available build options can be found in
> [options.cmake](https://github.com/LizardByte/Sunshine/blob/master/cmake/prep/options.cmake).

### Package

@tabs{
  @tab{FreeBSD | @tabs{
    @tab{pkg | ```bash
      cpack -G FREEBSD --config ./build/CPackConfig.cmake
      ```}
  }}
  @tab{Linux | @tabs{
    @tab{deb | ```bash
      cpack -G DEB --config ./build/CPackConfig.cmake
      ```}
    @tab{rpm | ```bash
      cpack -G RPM --config ./build/CPackConfig.cmake
      ```}
  }}
  @tab{macOS | @tabs{
    @tab{DragNDrop | ```bash
      cpack -G DragNDrop --config ./build/CPackConfig.cmake
      ```}
  }}
  @tab{Windows | @tabs{
    @tab{Installer | ```bash
      cpack -G NSIS --config ./build/CPackConfig.cmake # Not working on CLANGARM64
      ```}
    @tab{Portable | ```bash
      cpack -G ZIP --config ./build/CPackConfig.cmake
      ```}
  }}
}

### Remote Build
It may be beneficial to build remotely in some cases. This will enable easier building on different operating systems.

1. Fork the project
2. Activate workflows
3. Trigger the *CI* workflow manually
4. Download the artifacts/binaries from the workflow run summary

<div class="section_buttons">

| Previous                              |                            Next |
|:--------------------------------------|--------------------------------:|
| [Troubleshooting](troubleshooting.md) | [Contributing](contributing.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
