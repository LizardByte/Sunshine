%global build_timestamp %(date +"%Y%m%d")

# use sed to replace these values
%global build_version 0
%global branch 0
%global commit 0

%undefine _hardened_build

Name: Sunshine
Version: %{build_version}
Release: 1%{?dist}
Summary: Self-hosted game stream host for Moonlight.
License: GPLv3-only
URL: https://github.com/LizardByte/Sunshine
Source0: tarball.tar.gz

# BuildRequires: boost-devel >= 1.86.0
BuildRequires: cmake >= 3.25.0
BuildRequires: gcc
BuildRequires: gcc-c++
BuildRequires: libayatana-appindicator3-devel
BuildRequires: libcap-devel
BuildRequires: libcurl-devel
BuildRequires: libdrm-devel
BuildRequires: libevdev-devel
BuildRequires: libgudev
BuildRequires: libnotify-devel
BuildRequires: libva-devel
BuildRequires: libX11-devel
BuildRequires: libxcb-devel
BuildRequires: libXcursor-devel
BuildRequires: libXfixes-devel
BuildRequires: libXi-devel
BuildRequires: libXinerama-devel
BuildRequires: libXrandr-devel
BuildRequires: libXtst-devel
BuildRequires: git
BuildRequires: mesa-libGL-devel
BuildRequires: miniupnpc-devel
BuildRequires: npm
BuildRequires: numactl-devel
BuildRequires: openssl-devel
BuildRequires: opus-devel
BuildRequires: pulseaudio-libs-devel
BuildRequires: rpm-build
BuildRequires: systemd-udev
BuildRequires: systemd-rpm-macros
%{?sysusers_requires_compat}
BuildRequires: wget
BuildRequires: which

# for unit tests
BuildRequires: xorg-x11-server-Xvfb

# Conditional BuildRequires for cuda-gcc based on Fedora version
%if 0%{?fedora} >= 40
# this package conflicts with gcc on f39
BuildRequires: cuda-gcc-c++
%endif

Requires: libcap >= 2.22
Requires: libcurl >= 7.0
Requires: libdrm > 2.4.97
Requires: libevdev >= 1.5.6
Requires: libopusenc >= 0.2.1
Requires: libva >= 2.14.0
Requires: libwayland-client >= 1.20.0
Requires: libX11 >= 1.7.3.1
Requires: miniupnpc >= 2.2.4
Requires: numactl-libs >= 2.0.14
Requires: openssl >= 3.0.2
Requires: pulseaudio-libs >= 10.0
Requires: libayatana-appindicator3 >= 0.5.3

%description
Self-hosted game stream host for Moonlight.

%prep
# extract tarball to current directory
mkdir -p %{_builddir}/Sunshine
tar -xzf %{SOURCE0} -C %{_builddir}/Sunshine

# list directory
ls -a %{_builddir}/Sunshine

# patches
%autopatch -p1

%build
# Detect the architecture and Fedora version
architecture=$(uname -m)
fedora_version=%{fedora}

cuda_supported_architectures=("x86_64" "aarch64")

# set cuda_version based on Fedora version
# these are the same right now, but leave this structure to make it easier to set different versions
if [ "$fedora_version" == 39 ]; then
  cuda_version="12.6.2"
  cuda_build="560.35.03"
else
  cuda_version="12.6.2"
  cuda_build="560.35.03"
fi

# prepare CMAKE args
cmake_args=(
  "-B=%{_builddir}/Sunshine/build"
  "-G=Unix Makefiles"
  "-S=."
  "-DBUILD_DOCS=OFF"
  "-DBUILD_WERROR=ON"
  "-DCMAKE_BUILD_TYPE=Release"
  "-DCMAKE_INSTALL_PREFIX=%{_prefix}"
  "-DSUNSHINE_ASSETS_DIR=%{_datadir}/sunshine"
  "-DSUNSHINE_EXECUTABLE_PATH=%{_bindir}/sunshine"
  "-DSUNSHINE_ENABLE_WAYLAND=ON"
  "-DSUNSHINE_ENABLE_X11=ON"
  "-DSUNSHINE_ENABLE_DRM=ON"
  "-DSUNSHINE_PUBLISHER_NAME=LizardByte"
  "-DSUNSHINE_PUBLISHER_WEBSITE=https://app.lizardbyte.dev"
  "-DSUNSHINE_PUBLISHER_ISSUE_URL=https://app.lizardbyte.dev/support"
)

function install_cuda() {
  # check if we need to install cuda
  if [ -f "%{_builddir}/cuda/bin/nvcc" ]; then
    echo "cuda already installed"
    return
  fi

  if [ "$fedora_version" -ge 40 ]; then
    # update environment variables for CUDA, necessary when using cuda-gcc-c++
    export NVCC_PREPEND_FLAGS='-ccbin /usr/bin/cuda'
    export PATH=/usr/bin/cuda:"%{_builddir}/cuda/bin:${PATH}"
    export LD_LIBRARY_PATH="%{_builddir}/cuda/lib64:${LD_LIBRARY_PATH}"
  fi

  local cuda_prefix="https://developer.download.nvidia.com/compute/cuda/"
  local cuda_suffix=""
  if [ "$architecture" == "aarch64" ]; then
    local cuda_suffix="_sbsa"
  fi

  local url="${cuda_prefix}${cuda_version}/local_installers/cuda_${cuda_version}_${cuda_build}_linux${cuda_suffix}.run"
  echo "cuda url: ${url}"
  wget \
    "$url" \
    --progress=bar:force:noscroll \
    --retry-connrefused \
    --tries=3 \
    -q -O "%{_builddir}/cuda.run"
  chmod a+x "%{_builddir}/cuda.run"
  "%{_builddir}/cuda.run" \
    --no-drm \
    --no-man-page \
    --no-opengl-libs \
    --override \
    --silent \
    --toolkit \
    --toolkitpath="%{_builddir}/cuda"
  rm "%{_builddir}/cuda.run"
}

# we need to clear these flags to avoid linkage errors with cuda-gcc-c++
export CFLAGS=""
export CXXFLAGS=""
export FFLAGS=""
export FCFLAGS=""
export LDFLAGS=""

if [ -n "$cuda_version" ] && [[ " ${cuda_supported_architectures[@]} " =~ " ${architecture} " ]]; then
  install_cuda
  cmake_args+=("-DSUNSHINE_ENABLE_CUDA=ON")
  cmake_args+=("-DCMAKE_CUDA_COMPILER:PATH=%{_builddir}/cuda/bin/nvcc")
fi

# setup the version
export BRANCH=%{branch}
export BUILD_VERSION=v%{build_version}
export COMMIT=%{commit}

# cmake
cd %{_builddir}/Sunshine
echo "cmake args:"
echo "${cmake_args[@]}"
cmake "${cmake_args[@]}"
make -j$(nproc) -C "%{_builddir}/Sunshine/build"

%check
# run tests
cd %{_builddir}/Sunshine/build
xvfb-run ./tests/test_sunshine

%install
cd %{_builddir}/Sunshine/build
%make_install

# Add modules-load configuration
# load the uhid module in initramfs even if it doesn't detect the module as being used during dracut
# which must be run every time a new kernel is installed
install -D -m 0644 /dev/stdin %{buildroot}/usr/lib/modules-load.d/uhid.conf <<EOF
uhid
EOF

%post
# Note: this is copied from the postinst script
# Check if we're in an rpm-ostree environment
if [ ! -x "$(command -v rpm-ostree)" ]; then
  echo "Not in an rpm-ostree environment, proceeding with post install steps."

  # Trigger udev rule reload for /dev/uinput and /dev/uhid
  path_to_udevadm=$(which udevadm)
  if [ -x "$path_to_udevadm" ]; then
    echo "Reloading udev rules."
    $path_to_udevadm control --reload-rules
    $path_to_udevadm trigger --property-match=DEVNAME=/dev/uinput
    $path_to_udevadm trigger --property-match=DEVNAME=/dev/uhid
    echo "Udev rules reloaded successfully."
  else
    echo "error: udevadm not found or not executable."
  fi
else
  echo "rpm-ostree environment detected, skipping post install steps. Restart to apply the changes."
fi

%preun
# Remove modules-load configuration
rm -f /usr/lib/modules-load.d/uhid.conf

%files
# Executables
%caps(cap_sys_admin+p) %{_bindir}/sunshine
%caps(cap_sys_admin+p) %{_bindir}/sunshine-*

# Systemd unit file for user services
%{_userunitdir}/sunshine.service

# Udev rules
%{_udevrulesdir}/*-sunshine.rules

# Modules-load configuration
%{_modulesloaddir}/uhid.conf

# Desktop entries
%{_datadir}/applications/sunshine*.desktop

# Icons
%{_datadir}/icons/hicolor/scalable/apps/sunshine.svg
%{_datadir}/icons/hicolor/scalable/status/sunshine*.svg

# Metainfo
%{_datadir}/metainfo/sunshine.appdata.xml

# Assets
%{_datadir}/sunshine/**

%changelog
