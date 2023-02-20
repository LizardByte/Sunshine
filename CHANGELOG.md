# Changelog

## [0.18.4] - 2023-02-20
### Fixed
- (Linux/AUR) Drop support of AUR package
- (Docker) General enhancements to docker images

## [0.18.3] - 2023-02-13
### Added
- (Linux) Added PKGBUILD for Archlinux based distros to releases
- (Linux) Added precompiled package for Archlinux based distros to releases
- (Docker) Added archlinux docker image (x86_64 only)

## [0.18.2] - 2023-02-13
### Fixed
- (Video/KMV/Linux) Fixed wayland capture on Nvidia for KMS
- (Video/Linux) Implement vaSyncBuffer stuf for libva <2.9.0
- (UI) Fix issue where mime type was not being set for node_modules when using a reverse proxy
- (UI/macOS) Added missing audio sink config options
- (Linux) Specify correct Boost dependency versions
- (Video/AMF) Add missing encoder tunables

## [0.18.1] - 2023-01-31
### Fixed
- (Linux) Fixed missing dependencies for deb and rpm packages
- (Linux) Use dynamic boost

## [0.18.0] - 2023-01-29
Attention, this release contains critical security fixes. Please update as soon as possible. Additionally, we are
encouraging users to change your Sunshine password, especially if you expose the web UI (i.e. port 47790 by default)
to the internet, or have ever uploaded your logs with verbose output to a public resource.

### Added
- (Windows) Add support for Intel QuickSync
- (Linux) Added aarch64 deb and rpm packages
- (Windows) Add support for hybrid graphics systems, such as laptops with both integrated and discrete GPUs
- (Linux) Add support for streaming from Steam Deck Gaming Mode
- (Windows) Add HDR support, see https://docs.lizardbyte.dev/projects/sunshine/en/latest/about/usage.html#hdr-support
### Fixed
- (Network) Refactor code for UPnP port forwarding
- (Video) Enforce 10 FPS encoding frame rate minimum to improve static image quality
- (Linux) deb and rpm packages are now specific to destination distro and version
- (Docs) Add nvidia/nvenc preset migration guide
- (Network) Performance optimizations
- (Video/Windows) Fix streaming to multiple clients from hardware encoder
- (Linux) Fix child process spawning
- (Security) Fix security vulnerability in implementation of SimpleWebServer
- (Misc) Rename "Steam BigPicture" to "Steam Big Picture" in default apps.json
- (Security) Scrub basic authorization header from logs
- (Linux) The systemd service will now restart in the event of a crash
- (Video/KMS/Linux) Fixed error: `couldn't import RGB Image: 00003002 and 00003004`
- (Video/Windows) Fix stream freezing triggered by the resolution changed
- (Installer/Windows) Fixes silent installation and other miscellaneous improvements
- (CPU) Significantly improved CPU usage

## [0.17.0] - 2023-01-08
If you are running Sunshine as a service on Windows, we are strongly urging you to update to v0.17.0 as soon as
possible. Older Windows versions of Sunshine had a security flaw in which the binary was located in a user-writable
location which is problematic when running as a service or on a multi-user system. Additionally, when running Sunshine
as a service, games and applications were launched as SYSTEM. This could lead to issues with save files and other game
settings. In v0.17.0, games now run under your user account without elevated privileges.

### Breaking
- (Apps) Removed automatic desktop entry (Re-add by adding an empty application named "Desktop" with no commands, "desktop.png" can be added as the image.)
- (Windows) Improved user upgrade experience (Suggest to manually uninstall existing Sunshine version before this upgrade. Do NOT select to remove everything, if prompted. Make a backup of config files before uninstall.)
- (Windows) Move config files to specific directory (files will be migrated automatically if using Windows installer)
- (Dependencies) Fix npm path (breaking change for package maintainers)
### Added
- (macOS) Added initial support for arm64 on macOS through Macports portfile
- (Input) Added support for foreign keyboard input
- (Misc) Logs inside the WebUI and log to file
- (UI/Windows) Added an Apply button to configuration page when running as a service
- (Input/Windows) Enable Mouse Keys while streaming for systems with no physical mouse
### Fixed
- (Video) Improved capture performance
- (Audio) Improved audio bitrate and quality handling
- (Apps/Windows) Fixed PATH environment variable handling
- (Apps/Windows) Use the proper environment variable for the Program Files (x86) folder
- (Service/Windows) Fix SunshineSvc hanging if an error occurs during startup
- (Service/Windows) Spawn Sunshine.exe in a job object, so it is terminated if SunshineSvc.exe dies
- (Video) windows/vram: fix fringing in NV12 colour conversion
- (Apps/Windows) Launch games under the correct user account
- (Video) nvenc, amdvce: rework all user presets/options
- (Network) Generate certificates with unique serial numbers
- (Service/Windows) Graceful termination on shutdown, logoff, and service stop
- (Apps/Windows) Fix launching apps when Sunshine is running as admin
- (Misc) Remove/fix calls to std::abort()
- (Misc) Remove prompt to press enter after Sunshine exits
- (Misc) Make log priority consistent for execution messages
- (Apps) Applications in Moonlight clients are now updated automatically after editing
- (Video/Linux) Fix wayland capture on nvidia
- (Audio) Fix 7.1 surround channel mapping
- (Video) Fix NVENC profile values not applying
- (Network) Fix origin_web_ui_allowed binding
- (Service/Windows) Self terminate/restart service if process hangs for 10 seconds
- (Input/Windows) Fix Windows masked cursor blending with GPU encoders
- (Video) Color conversion fixes and BT.2020 support
### Dependencies
- Bump ffmpeg from 4.4 to 5.1
- ffmpeg_patches: add amfenc delay/buffering fix
- CBS moved to ffmpeg submodules
- Migrate to upstream Simple-Web-Server submodule
- Bump third-party/TPCircularBuffer from `bce9170` to `8833b3a`
- Bump third-party/moonlight-common-c from `8169a31` to `ef9ad52`
- Bump third-party/miniupnp from `6f848ae` to `207cf44`
- Bump third-party/ViGEmClient from `f719a1d` to `9e842ba`
- Bump bootstrap from 5.0.0 to 5.2.3
- Bump @fortawesome/fontawesome-free from 6.2.0 to 6.2.1

## [0.16.0] - 2022-12-13
### Added
- Add cover finder
- (Docker) Add arm64 docker image
- (Flatpak) Add installation helper scripts
- (Windows) Add support for Unicode input messages
### Fixed
- (Linux) Reintroduce Ubuntu 20.04 and 22.04 specific deb packages
- (Linux) Fixed udev and systemd file locations
### Dependencies
- Bump babel from 2.10.3 to 2.11.0
- Bump sphinx-copybutton from 0.5.0 to 0.5.1
- Bump KSXGitHub/github-actions-deploy-aur from 2.5.0 to 2.6.0
- Use npm for web dependencies (breaking change for third-party package maintainers)
- Update moonlight-common-c
- Use pre-built ffmpeg from LizardByte/build-deps for all sunshine builds (breaking change for third-party package maintainers)
- Bump furo from 2022.9.29 to 2022.12.7
### Misc
- Misc org level workflow updates
- Fix misc typos in docs
- Fix winget release

## [0.15.0] - 2022-10-30
### Added
- (Windows) Add firewall rules scripts
- (Windows) Automatically add and remove firewall rules at install/uninstall
- (Windows) Automatically add and remove service at install/uninstall
- (Docker) Official image added
- (Linux) Add aarch64 flatpak package
### Changed
- (Windows/Linux/MacOS) - Move default config and apps file to assets directory
- (MacOS) Bump boost to 1.80 for macport builds
- (Linux) Remove backup and restore of config files
### Fixed
- (Linux) - Create sunshine config directory if it doesn't exist
- (Linux) Remove portable home and config directories for AppImage
- (Windows) Include service install and uninstall scripts again
- (Windows) Automatically delete start menu entry upon uninstall
- (Windows) Automatically delete program install directory upon uninstall, with user prompt
- (Linux) Handle the case of no default audio sink
- (Windows/Linux/MacOS) Fix default image paths
- (Linux) Fix CUDA RGBA to NV12 conversion

## [0.14.1] - 2022-08-09
### Added
- (Linux) Flatpak package added
- (Linux) AUR package automated updates
- (Windows) Winget package automated updates
### Changed
- (General) Moved repo to @LizardByte GitHub org
- (WebUI) Fixed button spacing on home page
- (WebUI) Added Discord WidgetBot Crate
### Fixed
- (Linux/Mac) Default config and app files now copied to user home directory
- (Windows) Default config and app files now copied to working directory

## [0.14.0] - 2022-06-15
### Added
- (Documentation) Added Sphinx documentation available at https://sunshinestream.readthedocs.io/en/latest/
- (Development) Initial support for Localization
- (Linux) Add rpm package as release asset
- (macOS) Add Portfile as release asset
- (Windows) Add DwmFlush() call  to improve capture
- (Windows) Add Windows installer
### Fixed
- (AMD) Fixed hwdevice being destroyed before context
- (Linux) Added missing dependencies to AppImage
- (Linux) Fixed rumble events causing game to freeze
- (Linux) Improved Pulse/Pipewire compatibility
- (Linux) Moved to single deb package
- (macOS) Fixed missing TPCircularBuffer submodule
- (Stream) Properly catch exceptions in stream broadcast handlers
- (Stream/Video) AVPacket fix

## [0.13.0] - 2022-02-27
### Added
- (macOS) Initial support for macOS (#40)

## [0.12.0] - 2022-02-13
### Added
- New command line argument `--version`
- Custom png poster support
### Changed
- Correct software bitrate calculation
- Increase vbv-bufsize to 1/10 of requested bitrate
- Improvements to Web UI

## [0.11.1] - 2021-10-04
### Changed
- (Linux) Fix search path for config file and assets

## [0.11.0] - 2021-10-04
### Added
- (Linux) Added support for wlroots based compositors on Wayland.
- (Windows) Added an icon for the executable
### Changed
- Fixed a bug causing segfault when connecting multiple controllers.
- (Linux) Improved NVENC, it now offloads converting images from RGB to NV12
- (Linux) Fixed a bug causes stuttering

## [0.10.1] - 2021-08-21
### Changed
- (Linux) Re-enabled KMS

## [0.10.0] - 2021-08-20
### Added
- Added support for Rumble with gamepads.
- Added support for keyboard shortcuts <--- See the README for details.
- (Windows) A very basic script has been added in Sunshine-Windows\tools <-- This will start Sunshine at boot with the highest privileges which is needed to display the login prompt.
### Changed
- Some cosmetic changes to the WebUI.
- The first time the WebUI is opened, it will request the creation of a username/password pair from the user.
- Fixed audio crackling introduced in version 0.8.0
- (Linux) VAAPI hardware encoding now works on Intel i7-6700 at least. <-- For the best experience, using ffmpeg version 4.3 or higher is recommended.
- (Windows) Installing from debian package shouldn't overwrite your configuration files anymore. <-- It's recommended that you back up `/etc/sunshine/` before testing this.

## [0.9.0] - 2021-07-11
### Added
- Added audio encryption
- (Linux) Added basic NVENC support on Linux
- (Windows) The Windows version can now capture the lock screen and the UAC prompt as long as it's run through `PsExec.exe` https://docs.microsoft.com/en-us/sysinternals/downloads/psexec
### Changed
- Sunshine will now accept expired or not-yet-valid certificates, as long as they are signed properly.
- Fixed compatibility with iOS version of Moonlight
- Drastically reduced chance of being forced to skip error correction due to video frame size
- (Linux) sunshine.service will be installed automatically.

## [0.8.0] - 2021-06-30
### Added
- Added mDNS support: Moonlight will automatically find Sunshine.
- Added UPnP support. It's off by default.

## [0.7.7] - 2021-06-24
### Added
- (Linux) Added installation package for Debian
### Changed
- Fixed incorrect scaling for absolute mouse coordinates when using multiple monitors.
- Fixed incorrect colors when scaling for software encoder

## [0.7.1] - 2021-06-18
### Changed
- (Linux) Fixed an issue where it was impossible to start sunshine on ubuntu 20.04

## [0.7.0] - 2021-06-16
### Added
- Added a Web Manager. Accessible through: https://localhost:47990 or https://<ip of your pc>:47990
- (Linux) Added hardware encoding support for AMD on Linux
### Changed
- (Linux) Moved certificates and saved pairings generated during runtime to .config/sunshine on Linux

## [0.6.0] - 2021-05-26
### Added
- Added support for surround audio
### Changed
- Maintain aspect ratio when scaling video
- Fix issue where Sunshine is forced to drop frames when they are too large

## [0.5.0] - 2021-05-13
### Added
- Added support for absolute mouse coordinates
- (Linux) Added support for streaming specific monitor on Linux
- (Windows) Added support for AMF on Windows

## [0.4.0] - 2020-05-03
### Changed
- prep-cmd is now optional in apps.json
- Fixed bug causing video artifacts
- Fixed bug preventing Moonlight from closing app on exit
- Fixed bug causing preventing keyboard keys from repeating on latest version of Moonlight
- Fixed bug causing segfault when another session of sunshine was already running
- Fixed bug causing crash when monitor has resolution 1366x768

## [0.3.1] - 2020-04-24
### Changed
- Fix a memory leak.

## [0.3.0] - 2020-04-23
### Changed
- Hardware acceleration on NVidia GPU's for Video encoding on Windows

## [0.2.0] - 2020-03-21
### Changed
- Multicasting is now supported: You can set the maximum simultaneous connections with the configurable option: channels
- Configuration variables can be overwritten on the command line: "name=value" --> it can be useful to set min_log_level=debug without modifying the configuration file
- Switches to make testing the pairing mechanism more convenient has been added, see "sunshine --help" for details

## [0.1.1] - 2020-01-30
### Added
- (Linux) Added deb package and service for Linux

## [0.1.0] - 2020-01-27
### Added
- The first official release for Sunshine!

[0.1.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.1.0
[0.1.1]: https://github.com/LizardByte/Sunshine/releases/tag/v0.1.1
[0.2.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.2.0
[0.3.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.3.0
[0.3.1]: https://github.com/LizardByte/Sunshine/releases/tag/v0.3.1
[0.4.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.4.0
[0.5.0]: https://github.com/LizardByte/Sunshine/releases/tag/0.5.0
[0.6.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.6.0
[0.7.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.7.0
[0.7.1]: https://github.com/LizardByte/Sunshine/releases/tag/v0.7.1
[0.7.7]: https://github.com/LizardByte/Sunshine/releases/tag/v0.7.7
[0.8.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.8.0
[0.9.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.9.0
[0.10.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.10.0
[0.10.1]: https://github.com/LizardByte/Sunshine/releases/tag/v0.10.1
[0.11.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.11.0
[0.11.1]: https://github.com/LizardByte/Sunshine/releases/tag/v0.11.1
[0.12.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.12.0
[0.13.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.13.0
[0.14.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.14.0
[0.14.1]: https://github.com/LizardByte/Sunshine/releases/tag/v0.14.1
[0.15.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.15.0
[0.16.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.16.0
[0.17.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.17.0
[0.18.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.18.0
[0.18.1]: https://github.com/LizardByte/Sunshine/releases/tag/v0.18.1
[0.18.2]: https://github.com/LizardByte/Sunshine/releases/tag/v0.18.2
[0.18.3]: https://github.com/LizardByte/Sunshine/releases/tag/v0.18.3
