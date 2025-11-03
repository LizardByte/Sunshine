%global build_timestamp %(date +"%Y%m%d")

# use sed to replace these values
%global build_version 0
%global branch 0
%global commit 0

%undefine _hardened_build

# Define _metainfodir for OpenSUSE if not already defined
%if 0%{?suse_version}
%if !0%{?_metainfodir:1}
%global _metainfodir %{_datadir}/metainfo
%endif
%endif

Name: Sunshine
Version: %{build_version}
Release: 1%{?dist}
Summary: Self-hosted game stream host for Moonlight.
License: GPLv3-only
URL: https://github.com/LizardByte/Sunshine
Source0: tarball.tar.gz

# Common BuildRequires
BuildRequires: cmake >= 3.25.0
BuildRequires: desktop-file-utils
BuildRequires: git
BuildRequires: libcap-devel
BuildRequires: libcurl-devel
BuildRequires: libdrm-devel
BuildRequires: libevdev-devel
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
BuildRequires: npm
BuildRequires: openssl-devel
BuildRequires: rpm-build
BuildRequires: systemd-rpm-macros
BuildRequires: wget
BuildRequires: which

%if 0%{?fedora}
# Fedora-specific BuildRequires
BuildRequires: appstream
# BuildRequires: boost-devel >= 1.86.0
BuildRequires: libappstream-glib
BuildRequires: libayatana-appindicator3-devel
BuildRequires: libgudev
BuildRequires: mesa-libGL-devel
BuildRequires: mesa-libgbm-devel
BuildRequires: miniupnpc-devel
BuildRequires: numactl-devel
BuildRequires: opus-devel
BuildRequires: pulseaudio-libs-devel
BuildRequires: systemd-udev
%{?sysusers_requires_compat}
# for unit tests
BuildRequires: xorg-x11-server-Xvfb
%endif

%if 0%{?suse_version}
# OpenSUSE-specific BuildRequires
BuildRequires: AppStream
BuildRequires: appstream-glib
BuildRequires: libappindicator3-devel
BuildRequires: libgudev-1_0-devel
BuildRequires: Mesa-libGL-devel
BuildRequires: libgbm-devel
BuildRequires: libminiupnpc-devel
BuildRequires: libnuma-devel
BuildRequires: libopus-devel
BuildRequires: libpulse-devel
BuildRequires: udev
# for unit tests
BuildRequires: xvfb-run
%endif

# Conditional BuildRequires for cuda-gcc based on distribution version
%if 0%{?fedora}
%if 0%{?fedora} <= 41
BuildRequires: gcc13
BuildRequires: gcc13-c++
%global gcc_version 13
%global cuda_version 12.9.1
%global cuda_build 575.57.08
%elif %{?fedora} >= 42
BuildRequires: gcc14
BuildRequires: gcc14-c++
%global gcc_version 14
%global cuda_version 12.9.1
%global cuda_build 575.57.08
%endif
%endif

%if 0%{?suse_version}
%if 0%{?suse_version} <= 1699
# OpenSUSE Leap 15.x
BuildRequires: gcc13
BuildRequires: gcc13-c++
%global gcc_version 13
%global cuda_version 12.9.1
%global cuda_build 575.57.08
%else
# OpenSUSE Tumbleweed
BuildRequires: gcc14
BuildRequires: gcc14-c++
%global gcc_version 14
%global cuda_version 12.9.1
%global cuda_build 575.57.08
%endif
%endif

%global cuda_dir %{_builddir}/cuda

# Common runtime requirements
Requires: miniupnpc >= 2.2.4
Requires: which >= 2.21

%if 0%{?fedora}
# Fedora runtime requirements
Requires: libayatana-appindicator3 >= 0.5.3
Requires: libcap >= 2.22
Requires: libcurl >= 7.0
Requires: libdrm > 2.4.97
Requires: libevdev >= 1.5.6
Requires: libopusenc >= 0.2.1
Requires: libva >= 2.14.0
Requires: libwayland-client >= 1.20.0
Requires: libX11 >= 1.7.3.1
Requires: numactl-libs >= 2.0.14
Requires: openssl >= 3.0.2
Requires: pulseaudio-libs >= 10.0
%endif

%if 0%{?suse_version}
# OpenSUSE runtime requirements
Requires: libappindicator3-1
Requires: libcap2
Requires: libcurl4
Requires: libdrm2
Requires: libevdev2
Requires: libopusenc0
Requires: libva2
Requires: libwayland-client0
Requires: libX11-6
Requires: libnuma1
Requires: libopenssl3
Requires: libpulse0
%endif

%description
Self-hosted game stream host for Moonlight.

%prep
# extract tarball to current directory
mkdir -p %{_builddir}/Sunshine
tar -xzf %{SOURCE0} -C %{_builddir}/Sunshine

# list directory
ls -a %{_builddir}/Sunshine

%build
# exit on error
set -e

# Detect the architecture and Fedora version
architecture=$(uname -m)

cuda_supported_architectures=("x86_64" "aarch64")

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

export CC=gcc-%{gcc_version}
export CXX=g++-%{gcc_version}

function install_cuda() {
  # check if we need to install cuda
  if [ -f "%{cuda_dir}/bin/nvcc" ]; then
    echo "cuda already installed"
    return
  fi

  local cuda_prefix="https://developer.download.nvidia.com/compute/cuda/"
  local cuda_suffix=""
  if [ "$architecture" == "aarch64" ]; then
    local cuda_suffix="_sbsa"
  fi

  local url="${cuda_prefix}%{cuda_version}/local_installers/cuda_%{cuda_version}_%{cuda_build}_linux${cuda_suffix}.run"
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
    --toolkitpath="%{cuda_dir}"
  rm "%{_builddir}/cuda.run"

  # we need to patch math_functions.h on fedora 42+
  # see https://forums.developer.nvidia.com/t/error-exception-specification-is-incompatible-for-cospi-sinpi-cospif-sinpif-with-glibc-2-41/323591/3
  if [ "%{?fedora}" -ge 42 ]; then
    echo "Original math_functions.h:"
    find "%{cuda_dir}" -name math_functions.h -exec cat {} \;

    # Apply the patch
    patch -p2 \
      --backup \
      --directory="%{cuda_dir}" \
      --verbose \
      < "%{_builddir}/Sunshine/packaging/linux/patches/${architecture}/01-math_functions.patch"
  fi
}

if [ -n "%{cuda_version}" ] && [[ " ${cuda_supported_architectures[@]} " =~ " ${architecture} " ]]; then
  install_cuda
  cmake_args+=("-DSUNSHINE_ENABLE_CUDA=ON")
  cmake_args+=("-DCMAKE_CUDA_COMPILER:PATH=%{cuda_dir}/bin/nvcc")
  cmake_args+=("-DCMAKE_CUDA_HOST_COMPILER=gcc-%{gcc_version}")
else
  cmake_args+=("-DSUNSHINE_ENABLE_CUDA=OFF")
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
# validate the metainfo file
appstreamcli validate %{buildroot}%{_metainfodir}/*.metainfo.xml
appstream-util validate %{buildroot}%{_metainfodir}/*.metainfo.xml
desktop-file-validate %{buildroot}%{_datadir}/applications/*.desktop

# run tests
cd %{_builddir}/Sunshine/build
xvfb-run ./tests/test_sunshine

%install
cd %{_builddir}/Sunshine/build
%make_install

%post
# Note: this is copied from the postinst script

# Load uhid (DS5 emulation)
echo "Loading uhid kernel module for DS5 emulation."
modprobe uhid

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

%files
# Executables
%caps(cap_sys_admin+p) %{_bindir}/sunshine
%caps(cap_sys_admin+p) %{_bindir}/sunshine-*

# Systemd unit file for user services
%{_userunitdir}/sunshine.service

# Udev rules
%{_udevrulesdir}/*-sunshine.rules

# Modules-load configuration
%{_modulesloaddir}/*-sunshine.conf

# Desktop entries
%{_datadir}/applications/*.desktop

# Icons
%{_datadir}/icons/hicolor/scalable/apps/sunshine.svg
%{_datadir}/icons/hicolor/scalable/status/sunshine*.svg

# Metainfo
%{_datadir}/metainfo/*.metainfo.xml

# Assets
%{_datadir}/sunshine/**

%changelog
