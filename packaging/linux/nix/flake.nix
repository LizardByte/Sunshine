{
  description = "Self-hosted game stream host for Moonlight.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = inputs @ {self, ...}:
    inputs.flake-utils.lib.eachDefaultSystem (system: let
      pkgs = import (inputs.nixpkgs) {inherit system; config.allowUnfree = true;};

      inherit (inputs.nixpkgs) lib;
      inherit (pkgs) stdenv;

      nativeBuildInputs = with pkgs;
        [
          cmake
          pkg-config
          autoPatchelfHook
          makeWrapper
          cudaPackages.autoAddOpenGLRunpathHook
        ];

      buildInputs = with pkgs;
        [
          avahi
          libevdev
          libpulseaudio
          xorg.libX11
          xorg.libxcb
          xorg.libXfixes
          xorg.libXrandr
          xorg.libXtst
          xorg.libXi
          openssl
          libopus
          boost
          libdrm
          wayland
          # TODO: Audit me
          wayland-scanner
          libffi
          libevdev
          libcap
          libdrm
          curl
          pcre
          pcre2
          libuuid
          libselinux
          libsepol
          libthai
          libdatrie
          xorg.libXdmcp
          libxkbcommon
          libepoxy
          libva
          libvdpau
          numactl
          mesa
          amf-headers
          svt-av1
          libappindicator
          libnotify
          cudaPackages.cudatoolkit
          intel-media-sdk
          miniupnpc
        ];

      runtimeDependencies = with pkgs; [
        avahi
        mesa
        xorg.libXrandr
        xorg.libxcb
        libglvnd
      ];
      stdenv'= pkgs.cudaPackages.backendStdenv;

    in {
      packages.default = stdenv'.mkDerivation rec {
        inherit buildInputs nativeBuildInputs runtimeDependencies;

        name = "sunshine";
        src = ../../../.;
        version = self.shortRev or "dev";
        # fetch node_modules needed for webui
        ui = pkgs.buildNpmPackage {
          inherit src version;
          name = "sunshine-ui";
          npmDepsHash = "sha256-ITSeYy4bH3NpmcqNkunm9HgDmQG47RaLvnMIQO35pAE=";

          dontNpmBuild = true;

          # use generated package-lock.json as upstream does not provide one
          postPatch = ''
            cp ${../../../package-lock.json} ./package-lock.json
          '';

          installPhase = ''
            mkdir -p $out
            cp -r node_modules $out/
          '';
        };

        cmakeFlags = [
          "-Wno-dev"
          "-DSUNSHINE_REQUIRE_TRAY=OFF"
        ];

        postPatch = ''
          # fix hardcoded libevdev path
          substituteInPlace cmake/compile_definitions/linux.cmake \
            --replace '/usr/include/libevdev-1.0' '${pkgs.libevdev}/include/libevdev-1.0'

          substituteInPlace packaging/linux/sunshine.desktop \
            --replace '@PROJECT_NAME@' 'Sunshine' \
            --replace '@PROJECT_DESCRIPTION@' 'Self-hosted game stream host for Moonlight'
        '';
        preBuild = ''
          # copy node_modules where they can be picked up by build
          mkdir -p ../node_modules
          cp -r ${ui}/node_modules/* ../node_modules
        '';

        # allow Sunshine to find libvulkan
        postFixup = ''
          wrapProgram $out/bin/sunshine \
            --set LD_LIBRARY_PATH ${lib.makeLibraryPath [ pkgs.vulkan-loader ]}
        '';

        postInstall = ''
          install -Dm644 ../packaging/linux/${name}.desktop $out/share/applications/${name}.desktop
        '';
      };

      devShell = pkgs.mkShell {
        name = "sunshine-shell";
        inherit nativeBuildInputs;

        buildInputs = [
          buildInputs
          pkgs.git
          pkgs.cmake
          pkgs.nodejs
        ];

      };
    });
}
