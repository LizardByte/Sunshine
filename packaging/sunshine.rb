require "language/node"

class Sunshine < Formula
  CUDA_VERSION = "13.1".freeze
  CUDA_FORMULA = "cuda@#{CUDA_VERSION}".freeze
  GCC_VERSION = "14".freeze
  GCC_FORMULA = "gcc@#{GCC_VERSION}".freeze
  IS_UPSTREAM_REPO = ENV.fetch("GITHUB_REPOSITORY", "") == "LizardByte/Sunshine"

  desc "@PROJECT_DESCRIPTION@"
  homepage "@PROJECT_HOMEPAGE_URL@"
  url "@GITHUB_CLONE_URL@",
    tag: "@GITHUB_TAG@"
  version "@BUILD_VERSION@"
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

  bottle do
    root_url "https://ghcr.io/v2/lizardbyte/homebrew"
    sha256 arm64_tahoe:   "0000000000000000000000000000000000000000000000000000000000000000"
    sha256 arm64_sequoia: "0000000000000000000000000000000000000000000000000000000000000000"
    sha256 arm64_sonoma:  "0000000000000000000000000000000000000000000000000000000000000000"
    sha256 x86_64_linux:  "0000000000000000000000000000000000000000000000000000000000000000"
  end

  option "with-static-boost", "Enable static link of Boost libraries"
  option "without-static-boost", "Disable static link of Boost libraries" # default option

  depends_on "cmake" => :build
  depends_on "doxygen" => [:build, :recommended]
  depends_on "graphviz" => :build if build.with? "doxygen"
  depends_on "node" => :build
  depends_on "pkgconf" => :build
  depends_on "gcovr" => :test
  depends_on "boost"
  depends_on "curl"
  depends_on "icu4c@78"
  depends_on "miniupnpc"
  depends_on "openssl@3"
  depends_on "opus"

  on_macos do
    depends_on "llvm" => [:build, :test]
  end

  on_linux do
    depends_on GCC_FORMULA => [:build, :test]
    depends_on "lizardbyte/homebrew/#{CUDA_FORMULA}" => [:build, :recommended]
    depends_on "at-spi2-core"
    depends_on "avahi"
    depends_on "ayatana-ido"
    depends_on "cairo"
    depends_on "gdk-pixbuf"
    depends_on "glib"
    depends_on "gnu-which"
    depends_on "gtk+3"
    depends_on "harfbuzz"
    depends_on "libayatana-appindicator"
    depends_on "libayatana-indicator"
    depends_on "libcap"
    depends_on "libdbusmenu"
    depends_on "libdrm"
    depends_on "libice"
    depends_on "libnotify"
    depends_on "libsm"
    depends_on "libva"
    depends_on "libx11"
    depends_on "libxcb"
    depends_on "libxcursor"
    depends_on "libxext"
    depends_on "libxfixes"
    depends_on "libxi"
    depends_on "libxinerama"
    depends_on "libxrandr"
    depends_on "libxtst"
    depends_on "mesa"
    depends_on "numactl"
    depends_on "pango"
    depends_on "pipewire"
    depends_on "pulseaudio"
    depends_on "systemd"
    depends_on "wayland"
  end

  conflicts_with "sunshine-beta", because: "sunshine and sunshine-beta cannot be installed at the same time"
  fails_with :clang do
    build 1400
    cause "Requires C++23 support"
  end

  fails_with :gcc do
    version "12" # fails with GCC 12.x and earlier
    cause "Requires C++23 support"
  end

  def setup_build_environment
    ENV["BRANCH"] = "@GITHUB_BRANCH@"
    ENV["BUILD_VERSION"] = "@BUILD_VERSION@"
    ENV["COMMIT"] = "@GITHUB_COMMIT@"

    setup_linux_gcc_environment if OS.linux?
  end

  def setup_linux_gcc_environment
    # Use GCC because gcov from llvm cannot handle our paths
    gcc_path = Formula[GCC_FORMULA]
    ENV["CC"] = "#{gcc_path.opt_bin}/gcc-#{GCC_VERSION}"
    ENV["CXX"] = "#{gcc_path.opt_bin}/g++-#{GCC_VERSION}"
  end

  def base_cmake_args
    %W[
      -DBUILD_WERROR=ON
      -DCMAKE_CXX_STANDARD=23
      -DCMAKE_INSTALL_PREFIX=#{prefix}
      -DHOMEBREW_ALLOW_FETCHCONTENT=ON
      -DOPENSSL_ROOT_DIR=#{Formula["openssl"].opt_prefix}
      -DSUNSHINE_ASSETS_DIR=sunshine/assets
      -DSUNSHINE_BUILD_HOMEBREW=ON
      -DSUNSHINE_PUBLISHER_NAME='LizardByte'
      -DSUNSHINE_PUBLISHER_WEBSITE='https://app.lizardbyte.dev'
      -DSUNSHINE_PUBLISHER_ISSUE_URL='https://app.lizardbyte.dev/support'
    ]
  end

  def add_test_args(args)
    if IS_UPSTREAM_REPO
      args << "-DBUILD_TESTS=ON"
      ohai "Building tests: enabled"
    else
      args << "-DBUILD_TESTS=OFF"
      ohai "Building tests: disabled"
    end
  end

  def add_docs_args(args)
    if build.with? "doxygen"
      ohai "Building docs: enabled"
      args << "-DBUILD_DOCS=ON"
    else
      ohai "Building docs: disabled"
      args << "-DBUILD_DOCS=OFF"
    end
  end

  def add_boost_args(args)
    if build.without? "static-boost"
      args << "-DBOOST_USE_STATIC=OFF"
      ohai "Disabled statically linking Boost libraries"
    else
      configure_static_boost(args)
    end
  end

  def configure_static_boost(args)
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

  def add_cuda_args(args)
    return unless OS.linux?

    if build.with?(CUDA_FORMULA)
      configure_cuda(args)
    else
      args << "-DSUNSHINE_ENABLE_CUDA=OFF"
      ohai "CUDA disabled"
    end
  end

  def configure_cuda(args)
    cuda_path = Formula["lizardbyte/homebrew/#{CUDA_FORMULA}"]
    nvcc_path = "#{cuda_path.opt_bin}/nvcc"
    gcc_path = Formula[GCC_FORMULA]

    args << "-DSUNSHINE_ENABLE_CUDA=ON"
    args << "-DCMAKE_CUDA_COMPILER:PATH=#{nvcc_path}"
    args << "-DCMAKE_CUDA_HOST_COMPILER=#{gcc_path.opt_bin}/gcc-#{GCC_VERSION}"
    ohai "CUDA enabled with nvcc at: #{nvcc_path}"
  end

  def build_cmake_args
    args = base_cmake_args
    add_test_args(args)
    add_docs_args(args)
    add_boost_args(args)
    add_cuda_args(args)
    args
  end

  def build_and_install_project
    system "cmake", "-S", ".", "-B", "build", "-G", "Unix Makefiles",
            *std_cmake_args,
            *build_cmake_args

    system "make", "-C", "build"
    system "make", "-C", "build", "install"
  end

  def install_platform_specific_files
    bin.install "build/tests/test_sunshine" if IS_UPSTREAM_REPO

    # codesign the binary on intel macs
    system "codesign", "-s", "-", "--force", "--deep", bin/"sunshine" if OS.mac? && Hardware::CPU.intel?

    bin.install "src_assets/linux/misc/postinst" if OS.linux?
  end

  def install
    setup_build_environment
    build_and_install_project
    install_platform_specific_files
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

    if IS_UPSTREAM_REPO && ENV.fetch("HOMEBREW_BOTTLE_BUILD", "false") != "true"
      # run the test suite
      system bin/"test_sunshine", "--gtest_color=yes", "--gtest_output=xml:tests/test_results.xml"
      assert_path_exists File.join(testpath, "tests", "test_results.xml")

      # create gcovr report
      buildpath = ENV.fetch("HOMEBREW_BUILDPATH", "")
      unless buildpath.empty?
        # Change to the source directory for gcovr to work properly
        cd "#{buildpath}/build" do
          # Use GCC version to match what was used during compilation
          if OS.linux?
            gcc_path = Formula[GCC_FORMULA]
            gcov_executable = "#{gcc_path.opt_bin}/gcov-#{GCC_VERSION}"

            system "gcovr", ".",
              "-r", "../src",
              "--gcov-executable", gcov_executable,
              "--exclude-noncode-lines",
              "--exclude-throw-branches",
              "--exclude-unreachable-branches",
              "--xml-pretty",
              "-o=#{testpath}/coverage.xml"

            assert_path_exists File.join(testpath, "coverage.xml")
          end
        end
      end
    end
  end
end
