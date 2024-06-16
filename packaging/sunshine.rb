require "language/node"

class @PROJECT_NAME@ < Formula
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

  depends_on "cmake" => :build
  depends_on "node" => :build
  depends_on "pkg-config" => :build
  depends_on "curl"
  depends_on "miniupnpc"
  depends_on "openssl"
  depends_on "opus"

  on_linux do
    depends_on "libcap"
    depends_on "libdrm"
    depends_on "libnotify"
    depends_on "libva"
    depends_on "libvdpau"
    depends_on "libx11"
    depends_on "libxcb"
    depends_on "libxcursor"
    depends_on "libxfixes"
    depends_on "libxi"
    depends_on "libxinerama"
    depends_on "libxrandr"
    depends_on "libxtst"
    depends_on "numactl"
    depends_on "pulseaudio"
    depends_on "systemd"
    depends_on "wayland"
  end

  def install
    ENV["BRANCH"] = "@GITHUB_BRANCH@"
    ENV["BUILD_VERSION"] = "@BUILD_VERSION@"
    ENV["COMMIT"] = "@GITHUB_COMMIT@"

    args = %W[
      -DBUILD_WERROR=ON
      -DCMAKE_INSTALL_PREFIX=#{prefix}
      -DHOMEBREW_ALLOW_FETCHCONTENT=ON
      -DOPENSSL_ROOT_DIR=#{Formula["openssl"].opt_prefix}
      -DSUNSHINE_ASSETS_DIR=sunshine/assets
      -DSUNSHINE_BUILD_HOMEBREW=ON
      -DSUNSHINE_ENABLE_TRAY=OFF
      -DTESTS_ENABLE_PYTHON_TESTS=OFF
    ]
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args, *args

    cd "build" do
      system "make"
      system "make", "install"

      bin.install "tests/test_sunshine"
    end

    bin.install "src_assets/linux/misc/postinst" if OS.linux?
  end

  service do
    run [opt_bin/"sunshine", "~/.config/sunshine/sunshine.conf"]
  end

  def caveats
    caveats_message = <<~EOS
      Thanks for installing @PROJECT_NAME@!

      To get started, review the documentation at:
        https://docs.lizardbyte.dev/projects/sunshine/en/latest/
    EOS

    if OS.linux?
      caveats_message += <<~EOS
        ATTENTION: To complete installation, you must run the following command:
        `sudo #{bin}/postinst`
      EOS
    end

    if OS.mac?
      caveats_message += <<~EOS
        Sunshine can only access microphones on macOS due to system limitations.
        To stream system audio use "Soundflower" or "BlackHole".

        Gamepads are not currently supported on macOS.
      EOS
    end

    caveats_message
  end

  test do
    # test that the binary runs at all
    system "#{bin}/sunshine", "--version"

    # run the test suite
    # cannot build tests with python tests because homebrew destroys the source directory
    system "#{bin}/test_sunshine", "--gtest_color=yes"
  end
end
