{
  description = "Sunshine development environment with Linux AMF support";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };

        # FFmpeg with both static and shared libraries for Sunshine build
        ffmpeg-both = pkgs.ffmpeg-full.overrideAttrs (oldAttrs: {
          configureFlags = (oldAttrs.configureFlags or []) ++ [
            "--enable-static"
            "--enable-shared"
          ];
        });
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            # Build tools
            cmake
            ninja
            gcc
            gnumake
            pkg-config
            git
            
            # Core dependencies
            openssl
            curl
            boost.dev
            boost.out
            nlohmann_json
            
            # Graphics and video
            libva
            libdrm
            mesa
            vulkan-headers
            vulkan-loader
            vulkan-tools
            ffmpeg-both
            
            # AMD AMF support
            amf-headers
            amf
            
            # Audio
            pulseaudio
            libopus
            
            # Wayland/X11
            wayland
            wayland-protocols
            wayland-scanner
            libffi
            libxkbcommon
            xorg.libX11
            xorg.libXrandr
            
            # Additional dependencies
            avahi
            libuuid
            systemd
            libcap
            libevdev
            miniupnpc
            
            # Node.js for web assets
            nodejs
            
            # Documentation (optional)
            doxygen
            graphviz
          ];
          
          shellHook = ''
            echo "=== Sunshine Development Environment (Flake) ==="
            echo "All dependencies loaded!"
            echo "AMF Linux support: ENABLED"
            echo "Run: cd build && cmake .. && make -j$(nproc)"
            echo "================================================"
            
            # Set environment variables for AMF
            export AMF_ROOT="${amf}"
            export DRI_PRIME=1
            
            # Force shared boost libraries
            export Boost_USE_STATIC_LIBS=OFF
            export CMAKE_ARGS="-DBoost_USE_STATIC_LIBS=OFF"
            
            # Set up FFmpeg static libraries path
            export PKG_CONFIG_PATH="${ffmpeg-both}/lib/pkgconfig:$PKG_CONFIG_PATH"
            export FFMPEG_DIR="${ffmpeg-both}"
          '';
        };
      });
}