require "language/node"

class @PROJECT_NAME@ < Formula
  desc "@PROJECT_DESCRIPTION@"
  homepage "@PROJECT_HOMEPAGE_URL@"
  url "@GITHUB_CLONE_URL@",
    tag: "@GITHUB_BRANCH@"
  version "@PROJECT_VERSION@"
  license all_of: ["GPL-3.0-only"]
  head "@GITHUB_CLONE_URL@", branch: "nightly"

  depends_on "boost" => :build
  depends_on "cmake" => :build
  depends_on "node" => :build
  depends_on "pkg-config" => :build
  depends_on "curl"
  depends_on "miniupnpc"
  depends_on "openssl"
  depends_on "opus"

  def install
    args = %W[
      -DBUILD_WERROR=ON
      -DCMAKE_INSTALL_PREFIX=#{prefix}
      -DOPENSSL_ROOT_DIR=#{Formula["openssl"].opt_prefix}
      -DSUNSHINE_ASSETS_DIR=sunshine/assets
      -DSUNSHINE_BUILD_HOMEBREW=ON
      -DTESTS_ENABLE_PYTHON_TESTS=OFF
    ]
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args, *args

    cd "build" do
      system "make", "-j"
      system "make", "install"
      bin.install "tests/test_sunshine"
    end
  end

  service do
    run [opt_bin/"sunshine", "~/.config/sunshine/sunshine.conf"]
  end

  def caveats
    <<~EOS
      Thanks for installing @PROJECT_NAME@!

      To get started, review the documentation at:
        https://docs.lizardbyte.dev/projects/sunshine/en/latest/

      Sunshine can only access microphones on macOS due to system limitations.
      To stream system audio use "Soundflower" or "BlackHole".

      Gamepads are not currently supported on macOS.
    EOS
  end

  test do
    # test that the binary runs at all
    system "#{bin}/sunshine", "--version"

    # run the test suite
    # cannot build tests with python tests because homebrew destroys the source directory
    system "#{bin}/test_sunshine", "--gtest_color=yes"
  end
end
