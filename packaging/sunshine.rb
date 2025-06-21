require "language/node"

class @PROJECT_NAME@ < Formula
  # conflicts_with "sunshine", because: "sunshine and sunshine-beta cannot be installed at the same time"
  desc "@PROJECT_DESCRIPTION@"
  homepage "@PROJECT_HOMEPAGE_URL@"
  url "@GITHUB_CLONE_URL@",
    tag: "@GITHUB_TAG@"
  version "@FORMULA_VERSION@"
  license all_of: ["GPL-3.0-only"]
  head "@GITHUB_CLONE_URL@", branch: "@GITHUB_DEFAULT_BRANCH@"

  # https://docs.brew.sh/Brew-Livecheck#githublatest-strategy-block
  livecheck do
    url :stable
    regex(/^v?(\d+\.\d+\.\d+)$/i)
    strategy :github_latest do |json, regex|
      match = json["tag_name"]&.match(regex)
      next if match.blank?

      match[1]
    end
  end

  option "with-docs", "Enable docs"
  option "with-static-boost", "Enable static link of Boost libraries"
  option "without-static-boost", "Disable static link of Boost libraries" # default option

  depends_on "cmake" => :build
  depends_on "doxygen" => :build
  depends_on "graphviz" => :build
  depends_on "ninja" => :build
  depends_on "node" => :build
  depends_on "pkg-config" => :build
  depends_on "curl"
  depends_on "miniupnpc"
  depends_on "openssl"
  depends_on "opus"
  depends_on "icu4c" => :recommended

  on_linux do
    # the "build" dependencies are for libayatana-appindicator
    depends_on "at-spi2-core" => :build
    depends_on "cairo" => :build
    depends_on "fontconfig" => :build
    depends_on "freetype" => :build
    depends_on "fribidi" => :build
    depends_on "gettext" => :build
    depends_on "gobject-introspection" => :build
    depends_on "graphite2" => :build
    depends_on "gtk+3" => :build
    depends_on "harfbuzz" => :build
    depends_on "intltool" => :build
    depends_on "libepoxy" => :build
    depends_on "libxdamage" => :build
    depends_on "libxkbcommon" => :build
    depends_on "pango" => :build
    depends_on "perl" => :build
    depends_on "pixman" => :build
    depends_on "avahi"
    depends_on "libcap"
    depends_on "libdrm"
    depends_on "libnotify"
    depends_on "libva"
    depends_on "libx11"
    depends_on "libxcb"
    depends_on "libxcursor"
    depends_on "libxfixes"
    depends_on "libxi"
    depends_on "libxinerama"
    depends_on "libxrandr"
    depends_on "libxtst"
    depends_on "mesa"
    depends_on "numactl"
    depends_on "pulseaudio"
    depends_on "systemd"
    depends_on "wayland"

    # resources that do not have brew packages
    resource "libayatana-appindicator" do
      url "https://github.com/AyatanaIndicators/libayatana-appindicator/archive/refs/tags/0.5.94.tar.gz"
      sha256 "884a6bc77994c0b58c961613ca4c4b974dc91aa0f804e70e92f38a542d0d0f90"
    end

    resource "libdbusmenu" do
      url "https://launchpad.net/libdbusmenu/16.04/16.04.0/+download/libdbusmenu-16.04.0.tar.gz"
      sha256 "b9cc4a2acd74509435892823607d966d424bd9ad5d0b00938f27240a1bfa878a"

      patch 'From 729546c51806a1b3ea6cb6efb7a115b1baa811f1 Mon Sep 17 00:00:00 2001
From: =?UTF-8?q?Stefan=20Br=C3=BCns?= <stefan.bruens@rwth-aachen.de>
Date: Mon, 18 Nov 2019 19:58:53 +0100
Subject: [PATCH 1/1] Fix HAVE_VALGRIND AM_CONDITIONAL

The AM_CONDITIONAL should also be run with --disable-tests, otherwise
HAVE_VALGRIND is undefined.
---
 configure    | 4 ++--
 configure.ac | 2 +-
 2 files changed, 3 insertions(+), 3 deletions(-)

diff --git a/configure b/configure
index 831a3bb..8913b9b 100644
--- a/configure
+++ b/configure
@@ -14801,6 +14801,8 @@ else
         { $as_echo "$as_me:${as_lineno-$LINENO}: result: yes" >&5
 $as_echo "yes" >&6; }
 	have_valgrind=yes
+fi
+
 fi
  if test "x$have_valgrind" = "xyes"; then
   HAVE_VALGRIND_TRUE=
@@ -14811,8 +14813,6 @@ else
 fi


-fi
-



diff --git a/configure.ac b/configure.ac
index ace54d1..cbd38a6 100644
--- a/configure.ac
+++ b/configure.ac
@@ -120,8 +120,8 @@ PKG_CHECK_MODULES(DBUSMENUTESTS,  json-glib-1.0 >= $JSON_GLIB_REQUIRED_VERSION
                                   [have_tests=yes]
 )
 PKG_CHECK_MODULES(DBUSMENUTESTSVALGRIND, valgrind, have_valgrind=yes, have_valgrind=no)
-AM_CONDITIONAL([HAVE_VALGRIND], [test "x$have_valgrind" = "xyes"])
 ])
+AM_CONDITIONAL([HAVE_VALGRIND], [test "x$have_valgrind" = "xyes"])

 AC_SUBST(DBUSMENUTESTS_CFLAGS)
 AC_SUBST(DBUSMENUTESTS_LIBS)
--
2.46.2


'
    end

    resource "ayatana-ido" do
      url "https://github.com/AyatanaIndicators/ayatana-ido/archive/refs/tags/0.10.4.tar.gz"
      sha256 "bd59abd5f1314e411d0d55ce3643e91cef633271f58126be529de5fb71c5ab38"

      patch 'From 8a09e6ad33c58c017c0c8fd756da036fc39428ea Mon Sep 17 00:00:00 2001
From: Alexander Koskovich <akoskovich@pm.me>
Date: Sun, 29 Sep 2024 13:47:54 -0400
Subject: [PATCH 1/1] Make introspection configurable

---
 CMakeLists.txt     | 1 +
 src/CMakeLists.txt | 4 ++++
 2 files changed, 5 insertions(+)

diff --git a/CMakeLists.txt b/CMakeLists.txt
index 0e13fcd..f3e9ec0 100644
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -12,6 +12,7 @@ endif(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
 option(ENABLE_TESTS "Enable all tests and checks" OFF)
 option(ENABLE_COVERAGE "Enable coverage reports (includes enabling all tests and checks)" OFF)
 option(ENABLE_WERROR "Treat all build warnings as errors" OFF)
+option(ENABLE_INTROSPECTION "Enable introspection" ON)

 if(ENABLE_COVERAGE)
     set(ENABLE_TESTS ON)
diff --git a/src/CMakeLists.txt b/src/CMakeLists.txt
index 5b3638d..aca9481 100644
--- a/src/CMakeLists.txt
+++ b/src/CMakeLists.txt
@@ -108,6 +108,8 @@ install(TARGETS "ayatana-ido3-0.4" LIBRARY DESTINATION "${CMAKE_INSTALL_FULL_LIB

 # AyatanaIdo3-0.4.gir

+if (ENABLE_INTROSPECTION)
+
 find_package(GObjectIntrospection REQUIRED QUIET)

 if (INTROSPECTION_FOUND)
@@ -183,3 +185,5 @@ if (INTROSPECTION_FOUND)
     endif ()

 endif ()
+
+endif ()
--
2.46.2


'
    end

    resource "libayatana-indicator" do
      url "https://github.com/AyatanaIndicators/libayatana-indicator/archive/refs/tags/0.9.4.tar.gz"
      sha256 "a18d3c682e29afd77db24366f8475b26bda22b0e16ff569a2ec71cd6eb4eac95"
    end
  end

  def install
    ENV["BRANCH"] = "@GITHUB_BRANCH@"
    ENV["BUILD_VERSION"] = "@BUILD_VERSION@"
    ENV["COMMIT"] = "@GITHUB_COMMIT@"

    args = %W[
      -DBUILD_WERROR=ON
      -DCMAKE_CXX_STANDARD=20
      -DCMAKE_INSTALL_PREFIX=#{prefix}
      -DHOMEBREW_ALLOW_FETCHCONTENT=ON
      -DOPENSSL_ROOT_DIR=#{Formula["openssl"].opt_prefix}
      -DSUNSHINE_ASSETS_DIR=sunshine/assets
      -DSUNSHINE_BUILD_HOMEBREW=ON
      -DSUNSHINE_PUBLISHER_NAME='LizardByte'
      -DSUNSHINE_PUBLISHER_WEBSITE='https://app.lizardbyte.dev'
      -DSUNSHINE_PUBLISHER_ISSUE_URL='https://app.lizardbyte.dev/support'
    ]

    if build.with? "docs"
      ohai "Building docs: enabled"
      args << "-DBUILD_DOCS=ON"
    else
      ohai "Building docs: disabled"
      args << "-DBUILD_DOCS=OFF"
    end

    if build.without? "static-boost"
      args << "-DBOOST_USE_STATIC=OFF"
      ohai "Disabled statically linking Boost libraries"
    else
      args << "-DBOOST_USE_STATIC=ON"
      ohai "Enabled statically linking Boost libraries"

      unless Formula["icu4c"].any_version_installed?
        odie <<~EOS
          icu4c must be installed to link against static Boost libraries,
          either install icu4c or use brew install sunshine --with-static-boost instead
        EOS
      end
      ENV.append "CXXFLAGS", "-I#{Formula["icu4c"].opt_include}"
      icu4c_lib_path = Formula["icu4c"].opt_lib.to_s
      ENV.append "LDFLAGS", "-L#{icu4c_lib_path}"
      ENV["LIBRARY_PATH"] = icu4c_lib_path
      ohai "Linking against ICU libraries at: #{icu4c_lib_path}"
    end

    args << "-DCUDA_FAIL_ON_MISSING=OFF" if OS.linux?
    args << "-DSUNSHINE_ENABLE_TRAY=OFF" if OS.mac?

    # Handle system tray on Linux
    if OS.linux?
      # Build and install libayatana components

      # Build libdbusmenu
      resource("libdbusmenu").stage do
        system "./configure",
               "--prefix=#{prefix}",
               "--with-gtk=3",
               "--disable-dumper",
               "--disable-static",
               "--disable-tests",
               "--disable-gtk-doc",
               "--enable-introspection=no",
               "--disable-vala"
        system "make", "install"
      end

      # Build ayatana-ido
      resource("ayatana-ido").stage do
        system "cmake", "-S", ".", "-B", "build", "-G", "Ninja",
               "-DCMAKE_INSTALL_PREFIX=#{prefix}",
               "-DENABLE_INTROSPECTION=OFF",
               *std_cmake_args
        system "ninja", "-C", "build"
        system "ninja", "-C", "build", "install"
      end

      # Build libayatana-indicator
      resource("libayatana-indicator").stage do
        ENV.append_path "PKG_CONFIG_PATH", "#{lib}/pkgconfig"
        ENV.append "LDFLAGS", "-L#{lib}"

        system "cmake", "-S", ".", "-B", "build", "-G", "Ninja",
               "-DCMAKE_INSTALL_PREFIX=#{prefix}",
               *std_cmake_args
        system "ninja", "-C", "build"
        system "ninja", "-C", "build", "install"
      end

      # Build libayatana-appindicator
      resource("libayatana-appindicator").stage do
        system "cmake", "-S", ".", "-B", "build", "-G", "Ninja",
               "-DCMAKE_INSTALL_PREFIX=#{prefix}",
               "-DENABLE_BINDINGS_MONO=OFF",
               "-DENABLE_BINDINGS_VALA=OFF",
               "-DENABLE_GTKDOC=OFF",
               *std_cmake_args
        system "ninja", "-C", "build"
        system "ninja", "-C", "build", "install"
      end
    end

    system "cmake", "-S", ".", "-B", "build", "-G", "Unix Makefiles",
            *std_cmake_args,
            *args

    system "make", "-C", "build"
    system "make", "-C", "build", "install"
    bin.install "build/tests/test_sunshine"

    # codesign the binary on intel macs
    system "codesign", "-s", "-", "--force", "--deep", bin/"sunshine" if OS.mac? && Hardware::CPU.intel?

    bin.install "src_assets/linux/misc/postinst" if OS.linux?
  end

  service do
    run [opt_bin/"sunshine", "~/.config/sunshine/sunshine.conf"]
  end

  def post_install
    if OS.linux?
      opoo <<~EOS
        ATTENTION: To complete installation, you must run the following command:
        `sudo #{bin}/postinst`
      EOS
    end

    if OS.mac?
      opoo <<~EOS
        Sunshine can only access microphones on macOS due to system limitations.
        To stream system audio use "Soundflower" or "BlackHole".

        Gamepads are not currently supported on macOS.
      EOS
    end
  end

  def caveats
    <<~EOS
      Thanks for installing @PROJECT_NAME@!

      To get started, review the documentation at:
        https://docs.lizardbyte.dev/projects/sunshine
    EOS
  end

  test do
    # test that the binary runs at all
    system bin/"sunshine", "--version"

    # run the test suite
    system bin/"test_sunshine", "--gtest_color=yes", "--gtest_output=xml:test_results.xml"
    assert_path_exists testpath/"test_results.xml"
  end
end
