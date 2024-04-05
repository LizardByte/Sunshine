# Changelog

## [0.23.0] - 2024-04-06
Attention, this release contains critical security fixes. Please update as soon as possible.

**Breaking**
- (Linux) Drop support for Ubuntu 20.04
- (Linux) No longer provide arm64 rpm packages, due to extreme compile time on GitHub hosted runners

**Fixed**
- (Network) Ensure unpairing takes effect without restart
- (Capture/Linux) Fix logical comparison of texture size
- (Service/Windows) Quote the path to sunshinesvc.exe when launching the termination helper

**Added**
- (WebUI) Localization support
- (Capture/Linux) Populate host latency for kmx/x11 grab
- (Capture/Windows) AMF rate control improvements
- (Linux) Add support for Ubuntu 24.04 (x86_64 only)

**Dependencies**
- Bump rstcheck from 6.2.0 to 6.2.1
- Bump org.flatpak.Builder.BaseApp from 644487f to 6e295e6
- Bump ffmpeg
- Bump @fortawesome/fontawesome-free from 6.5.1 to 6.5.2

**Misc**
- (Style) Refactored video encoder declarations
- (CI) Refactored Linux build in CI
- (CI) Added unit testing and code coverage
- (Docs/macOS) Update curl command for Portfile install
- (Style) Refactor logging initialization


## [0.22.2] - 2024-03-15
**Fixed**
- (Tray/Windows) Fix broken system tray icon on some systems
- (Linux) Fix crash when XDG_CONFIG_HOME or CONFIGURATION_DIRECTORY are set
- (Linux) Fix config migration across filesystems and with non-existent parent directories

## [0.22.1] - 2024-03-13
**Breaking**
- (ArchLinux) Drop support for standalone PKGBUILD files. Use the binary Arch package or install via AUR instead.
- (macOS) Drop support for experimental dmg package. Use Homebrew or MacPorts instead.

**Added**
- (macOS) Added Homebrew support

**Changed**
- (Process/Windows) The working directory is now searched first when the command contains a relative path
- (ArchLinux) The kmsgrab capture backend is now compiled by default to support Wayland capture on non-wlroots-based compositors
- (Capture/Linux) X11 capture is now preferred over kmsgrab for cards that lack atomic modesetting support to ensure cursor capture works
- (Capture/Linux) Kmsgrab will only choose NVENC by default if the display is connected to the Nvidia GPU to avoid possible EGL import failures

**Fixed**
- (Config) Fix unsupported resolution error with some Moonlight clients
- (Capture/Windows) Fix crash when streaming Ryujinx, Red Alert 2, and other apps that use unusually sized monochrome cursors
- (Capture/Linux) Fix crash in KMS cursor capture when running on Arch-based distros
- (Capture/Linux) Fix crash if CUDA GPU has a PCI ID with hexadecimal digits greater than 9
- (Process/Windows) Fix starting apps when the working directory is enclosed in quotes
- (Process/Windows) Fix process tree tracking when the app is launched via a cmd.exe trampoline
- (Installer/Windows) Fix slow operation during ViGEmBus installation that may cause the installer to appear stuck
- (Build/macOS) Fix issues building on macOS 13 and 14
- (Build/Linux) Fix missing install script in the Arch binary package
- (Build/Linux) Fix missing optional dependencies in the Arch binary package
- (Build/Linux) Ensure correct Arch pkg is published to GitHub releases
- (Capture/Linux) Fix mismatched case and unhandled exception in CUDA device lookup
- (Config) Add missing resolution to default config ui
- (Linux) Fix udev rules for uinput access not working until after reboot
- (Linux) Fix wrong path in desktop files
- (Tray) Cache icons to avoid possible DRM issues
- (Tray) Fix attempt to update tray icon after it was destroyed
- (Linux) Migrate old config files to new location if env SUNSHINE_MIGRATE_CONFIG=1 is set (automatically set for Flatpak)
- (Linux/Fedora) Re-enable CUDA support and bump to 12.4.0

**Misc**
- (Build/Windows) Adjust Windows debuginfo artifact to reduce confusion with real release binaries

## [0.22.0] - 2024-03-03
**Breaking**
- (Network) Clients must now be paired with the host before they can use Wake-on-LAN
- (Build/Linux) Drop Fedora 37 support

**Added**
- (Input/Linux) Add native/pen touch support for Linux
- (Capture/Linux) Add HDR streaming support for Linux using KMS capture backend
- (Capture/Linux) Add KMS capture support for Nvidia GPUs running Wayland
- (Network) Add support for full E2E stream encryption, configurable for LAN and WAN independently
- (Process) Add process group tracking to automatically handle launchers that spawn other child processes
- (Capture/Windows) Add setting for controlling GPU power saving and encoding latency tradeoff for NVENC
- (Capture/Windows) Add additional encoding settings for NVENC
- (Process/Windows) Add experimental support for launching URLs and other non-exe files
- (Capture/Windows) Add setting to allow use of slower HEVC encoding on older Intel GPUs
- (Input/Windows) Add settings to control automatic gamepad type selection heuristics
- (Input/Windows) Add setting to allow DS4 back/select button to trigger touchpad click
- (Input) Add setting to disable high resolution scrolling and native pen/touch support
- (Network) Add support for certificates types other than RSA-2048
- (Build/Linux) Add Fedora 39 docker image and rpm package
- (Capture/Linux) Display monitor indexes in logs for wlroots and KMS capture backends
- (UI) Add link to logs inside fatal error container
- (UI) Add hash handler and ids for all configuration categories and settings

**Changed**
- (UI) Several configuration options have been moved to more suitable locations
- (Network) Client-selected bitrate is now adjusted for FEC percentage and other stream overhead
- (Capture/Linux) Improve VAAPI encoding performance on Intel GPUs
- (Capture) Connection establishment delay is reduced by eliminating many encoder probing operations
- (Process) Graceful termination of running processes is attempted first when stopping apps
- (Capture) Improve software encoding performance by enabling multi-threaded color conversion
- (Capture) Adjust default CPU thread count for software encoding from 1 to 2 for improved performance
- (Steam/Windows) Modernized the default Steam app shortcut to avoid depending on Steam's install location and support app termination
- (Linux) Updated desktop files
- (Config) Add 2560x1440 to default resolutions
- (Network) Use the configured ping timeout for the initial launch event timeout
- (UI) Migrate UI to Vite and Vue3, and various UX improvements
- (Logging) Adjust wording and severity of some log messages
- (Build) Use a single submodule for ffmpeg
- (Install/Windows) Skip ViGEmBus installation if a supported version is already installed
- (Build/Linux) Optionally, allow using the system installation of wayland-protocols
- (Build/Linux) Make vaapi optional
- (Windows) Replace boost::json with nlohmann/json

**Fixed**
- (Network/Windows) Fix auto-discovery of hosts by iOS/tvOS clients
- (Network) Fix immediate connection termination when streaming over some Internet connections
- (Capture/Linux) Fix missing mouse cursor when using KMS capture on a GPU with hardware cursor support
- (Capture/Windows) Add workaround for Nvidia driver bug causing Sunshine to crash when RTX HDR is globally enabled
- (Capture/Windows) Add workaround for AMD driver bug on pre-RDNA GPUs causing hardware encoding failure
- (Capture/Windows) Reintroduce support for NVENC on older Nvidia GPU drivers (v456.71-v522.25)
- (Capture/Windows) Fix encoding on old Intel GPUs that don't support low-power H.264 encoding
- (Capture/Linux) Fix GL errors or corrupt video output on GPUs that use aux planes such as Intel Arc
- (Capture/Linux) Fix GL errors or corrupt video output on GPUs that use DRM modifiers on YUV buffers
- (Input/Windows) Fix non-functional duplicate controllers appearing in rare cases
- (Input/Windows) Avoid triggering crash in ViGEmBus when the system goes to sleep
- (Input/Linux) Fix scrolling in applications that don't support high-resolution scrolling
- (Input/Linux) Fix absolute mouse input being interpreted as touch input
- (Capture/Linux) Fix wlroots capture causing GL errors and crashes
- (Capture/Linux) Fix wlroots capture failing when the display scale factor was not 1
- (Capture/Linux) Fix excessive CPU usage when using wlroots capture backend
- (Capture/Linux) Fix capture of virtual displays created by the amdgpu kernel driver
- (Audio/Windows) Fix audio capture failures on Insider Preview versions of Windows 11
- (Capture/Windows) Fix incorrect portrait mode rotation
- (Capture/Windows) Fix capture recovery when a driver update/crash occurs while streaming
- (Capture/Windows) Fix delay displaying UAC dialogs when the mouse cursor is not moving
- (Capture/Linux) Fix corrupt video output or stream disconnections if the display resolution changes while streaming
- (Capture/Linux) Fix color of aspect ratio padding in the capture image with VAAPI
- (Capture/Linux) Fix NVENC initialization error when using X11 capture with some GPUs
- (Tray/Linux) Fix random crash when the tray icon is updating
- (Network) Fix QoS tagging when running in IPv4+IPv6 mode
- (Process) Fix termination of child processes upon app quit when the parent has already terminated
- (Process) Fix notification of graceful termination to connected clients when Sunshine quits
- (Capture) Fix corrupt output or green aspect-ratio padding when using software encoding with some video resolutions
- (Windows) Fix crashes when processing file paths or other strings with certain non-ASCII characters
- (Capture) Ensure user supplied framerates are used exclusively in place of pre-defined framerates
- (CMake/Linux) Skip including unnecessary headers
- (Capture/Linux) Replace vaTerminate method with dl handle
- (Capture/Linux) Fix capture when DRM is enabled and x11 is disabled
- (Tray) Use PROJECT_NAME definition for tooltip
- (CMake) Use GNUInstallDirs to install data and lib directories
- (macOS) Replace deprecated code
- (API) Allow trailing slashes in on API endpoints
- (API) Add additional pin validation
- (Linux) Use XDG spec for fetching config directory
- (CMake) Properly find evdev
- (Config) Properly save global_prep_cmd and fps settings

**Dependencies**
- Bump third-party/wayland-protocols from 681c33c to 46f201b
- Bump third-party/nv-codec-headers from 9402b5a to 22441b5
- Bump third-party/nanors from 395e5ad to e9e242e
- Bump third-party/Simple-Web-Server from 2f29926 to 27b41f5
- Bump ffmpeg
- Bump third-party/tray from 2664388 to 2bf1c61
- Bump actions/setup-python from 4 to 5
- Bump actions/upload-artifact from 3 to 4
- Bump @fortawesome/fontawesome-free from 6.4.2 to 6.5.1
- Bump babel from 2.13.0 to 2.14.0
- Move miniupnpc from submodule to system installed package
- Bump furo from 2023.9.10 to 2024.1.29
- Bump third-party/moonlight-common-c from f78f213 to cbd0ec1
- Bump third-party/ViGEmClient from 1920260 to 8d71f67
- Bump peter-evans/create-pull-request from 5 to 6
- Bump bootstrap from 5.3.2 to 5.3.3

**Misc**
- (Build) Update global workflows
- (Docs/Linux) Add example for setting custom resolution with NVIDIA
- (Docs) Fix broken links
- (Docs/Windows) Add information about disk permissions
- (Docs) Fix failing images
- (Docs) Use glob pattern to match source code docs
- (CI/macOS) Install boost from source
- (Docs) Add reset credentials examples for unique packages
- (Docs) Refactor and general cleanup
- (Docs) Cross-reference config settings to the UI
- (Docs/Docker) Add podman notes
- (Build) Use CMAKE_SOURCE_DIR property everywhere
- (Build/Docker) Add docker toolchain file for CLion
- (macOS) Various code style fixes
- (Deps) Alphabetize git submodules
- (Docs/Examples) Update URI examples
- (Refactor) Refactored some code in preparation for unit testing implementation
- (CMake) Add option to skip cuda inheriting compile options
- (CMake) Add option to error build on warnings

## [0.21.0] - 2023-10-15
**Added**
- (Input) Add support for automatically selecting the emulated controller type based on the physical controller connected to the client
- (Input/Windows) Add support for Applications (context menu) key
- (Input/Windows) Implement touchpad, motion sensors, battery state, and LED control for the emulated DualShock 4 controller
- (Input) Advertise support for new input features to clients
- (Linux/Debian) Added Debian Bookworm package
- (Prep-Commands) Expose connection environment variables
- (Input/Windows) Implement pen and touch support
- (Capture/Windows) Add standalone NVENC encoder
- (Capture) Implement AV1 encoding
- (Network) Implement IPv6 support
- (Capture/Windows) Add option to disable realtime hags
- (Graphics/NVIDIA) Add an option to decrease GPU scheduling priority to workaround HAGS video hang
- (Capture/Linux) Add FFmpeg powerpc64le architecture for self compiling Sunshine
- (Capture/Windows) Add support for capturing rotated displays
- (System Tray) Implement streaming event notifications
- (UI) Add port configuration table
- (Applications) Added option to automatically treat launcher type apps as detached commands
- (Input/Gamepad) Allow the Misc button to work as Guide on emulated Xbox 360 controllers

**Changed**
- (Input) Reduce latency by implementing input batching
- (Logging) Move input packet debug prints off the control stream thread
- (Input) Refactor gamepad emulation code to use DS4 extended report format
- (Graphics/NVIDIA) Modify and restore NVIDIA control panel settings before and after stream, respectively
- (Graphics/NVIDIA) New config page for NVENC
- (Graphics/Windows) Refactor DX shaders
- (Input/Windows) Use our own keycode mapping to avoid installing the US English keyboard layout

**Fixed**
- (UI) Fix update notifications
- (Dependencies/Linux) Replace libboost chrono and thread with standard chrono and thread
- (Input) Increase maximum gamepad limit to 16
- (Network) Allow use of multiple ENet channels
- (Network) Consider link-local addresses on LAN
- (Input) Fixed issue where button may sometimes stick on Windows
- (Input) Fix "ControllerNumber not allocated" warning when a gamepad is removed
- (Input) Fix handling of gamepad feedback with multiple clients connected
- (Input) Fix clamping mouse position to aspect ratio adjusted viewport
- (Graphics/AMD) Fix crash during startup on some older AMD GPUs
- (Logging) Fix crash when non-ASCII characters are logged
- (Prep-Commands) Fix resource exhaustion bug which could occur when many prep commands were used
- (Subprocesses) Fix race condition when inserting new processes
- (Logging) Log error if encoder doesn't produce IDR frame on demand
- (Audio) Improve audio capture logic and logging
- (Logging) Fix AMF logging to match configured log level
- (Logging) Log FFmpeg to log file instead of stdout
- (Capture) Reject codecs that are not supported by display device
- (Capture) Add fallbacks for unsupported codec settings
- (Capture) Avoid probing HEVC or AV1 codecs in some cases
- (Caputre) Remove DwmFlush()
- (Capture/Windows) Improvements to capture sleeps for better frame stability
- (Capture/Windows) Adjust capture rate to better match with display
- (Linux/ArchLinux) Fix package version in PKGBUILD and precompiled package 
- (UI) Highlight fatal log messages in web ui
- (Commands) Allow stream if prep command fails
- (Capture/Linux) Fix KMS grab VRAM capture with libva 2.20
- (Capture/macOS) Fix video capture backend
- (Misc/Windows) Don't start the session monitor window when launched in command mode
- (Linux/AppImage) Use the linuxdeploy GTK plugin to correctly deploy GTK3 dependencies
- (Input/Windows) Fix reWASD not recognizing emulated DualShock 4 input

**Dependencies**
- Bump bootstrap from 5.2.3 to 5.3.2
- Bump third-party/moonlight-common-c from c9426a6 to 7a6d12f
- Bump gcc-10 in Ubuntu 20.04 docker image
- Bump furo from 2023.5.20 to 2023.9.10
- Bump sphinx from 7.0.1 to 7.2.6
- Bump cmake from 3.26 to 3.27 in Fedora docker images
- Move third-party/nv-codec-headers from sdk/11.1 branch to sdk/12.0 branch
- Automatic bump ffmpeg
- Bump actions/checkout from 3 to 4
- Bump boost from 1.80 to 1.81 in Macport manifest
- Bump @fortawesome/fontawesome-free from 6.4.0 to 6.4.2

**Misc**
- (Docs) Force badges to use svg
- (Docs) Add Linux SSH example
- (Docs) Add information about mesa for Linux
- (CI) Free additional space on Docker, Flatpak, and AppImage builds due to internal changes on GitHub runners
- (Docs/Logging/UI) Corrected various typos
- (Docs) Add blurb about Gamescope compatibility
- (Installer/Windows) Use system proxy to download ViGEmBus
- (CI) Ignore third-party directory for clang-format
- (Docs/Linux) Add Plasma-Compatible resolution example
- (Docs) Add Sunshine website available at https://app.lizardbyte.dev/Sunshine
- (Build/Windows) Fix audio code build with new MinGW headers
- (Build/Windows) Fix QoS code build with new MinGW headers
- (CI/Windows) Prevent winget action from creating an update when running in a fork
- (CI/Windows) Change winget job to ubuntu-latest runner
- (CI) Add CodeQL analysis
- (CI/Docker) Fix ArchLinux image caching issue
- (Windows) Manifest improvements
- (CI/macOS) Simplify macport build
- (Docs) Remove deprecated options from readthedocs config
- (CI/Docs) Lint rst files
- (Docs) Update localization information (after consolidating Crowdin projects)
- (Cmake) Split CMakelists into modules
- (Docs) Add Linux Headless/SSH Guide

## [0.20.0] - 2023-05-28
**Breaking**
- (Windows) The Windows installer version of Sunshine is now always launched by the Sunshine Service. Manually launching Sunshine.exe from Program Files is no longer supported. This was necessary to address security issues caused by non-admin users having access to Sunshine's config data. If you have set up Task Scheduler or other mechanisms to launch Sunshine automatically, remove those from your system before updating.
- (Windows) The Start Menu shortcut has been redesigned for use with the Sunshine Service. It now launches Sunshine in the background (if not already running) and opens the Web UI, avoiding the persistent Command Prompt window present in prior versions. The Start Menu shortcut is now the recommended method for opening the Web UI and launching Sunshine.
- (Network/UPnP) If the Moonlight Internet Hosting Tool is installed alongside Sunshine, you must remove it or upgrade to v5.6 or later to prevent conflicts with Sunshine's UPnP support. As a reminder, the Moonlight Internet Hosting Tool is not required to stream over the Internet with Sunshine. Instead, simply enable UPnP in the Sunshine Web UI.
- (Windows) If Steam is installed, the Steam Streaming Speakers driver will be automatically installed when starting a stream for the first time. This behavior can be disabled in the Audio/Video tab of the Web UI. This Steam driver enables support for surround sound and muting host audio without requiring any manual configuration.
- (Input) The Back Button Timeout option has been renamed to Guide Button Emulation Timeout and has been disabled by default to ensure long presses on the Back button work by default. The previous behavior can be restored by setting the Guide Button Emulation Timeout to 2000.
- (Windows) The service name of SunshineSvc has been changed to SunshineService to address a false positive in MalwareBytes. If you have any scripts or other logic on your system that is using the service name, you will need to update that for the new name.
- (Windows) To support new installer features, install-service.bat no longer sets the service to auto-start by default. Users executing install-service.bat manually on the Sunshine portable build must also execute autostart-service.bat to start Sunshine on boot. However, installing the service manually like this is not recommended. Instead, use the Sunshine installer which handles service installation and configuration automatically.
- (Linux/Fedora) Fedora 36 builds are removed due to upstream end of support

**Added**
- (Windows) Added an option to launch apps and prep/undo commands as administrator
- (Installer/Windows) Added an option to choose whether Sunshine launches on boot. If not configured to launch on boot, use the Start Menu shortcut to start Sunshine when desired.
- (Input/Windows) Added option to send VK codes instead of scancodes for compatibility with iOS/Android devices using non-English keyboard layouts
- (UI) The Apply/Restart option is now available in the Web UI for all platforms
- (Systray) Added a Restart option to the system tray context menu
- (Video/Windows) Added host latency data to video frames. This requires future updates to Moonlight to display in the on-screen overlay.
- (Audio) Added support for matching Audio Sink and Virtual Sink values on device names
- (Client) Added friendly error messages for clients when streaming fails to start
- (Video/Windows) Added warning log messages when Windows is hiding DRM-protected content from display capture
- (Interop/Windows) Added warning log messages when GeForce Experience is currently using the same ports as Sunshine
- (Linux/Fedora) Fedora 38 builds are now available

**Changed**
- (Video) Encoder selection now happens at each stream start for more reliable GPU detection
- (Video/Windows) The host display now stays on while clients are actively streaming
- (Audio) Streaming will no longer fail if audio capture is unavailable
- (Audio/Windows) Sunshine will automatically switch back to the Virtual Sink if the default audio device is changed while streaming
- (Audio) Changes to the host audio playback option will now take effect when resuming a session from Moonlight
- (Audio/Windows) Sunshine will switch to a different default audio device if Steam Streaming Speakers are the default when Sunshine starts. This handles cases where Sunshine terminates unexpectedly without restoring the default audio device.
- (Apps) The Connection Terminated dialog will no longer appear in Moonlight when the app on the host exits normally
- (Systray/Windows) Quitting Sunshine via the system tray will now stop the Sunshine Service rather than leaving it running and allowing it to restart Sunshine
- (UI) The 100.64.0.0/10 CGN IP address range is now treated as a LAN address range
- (Video) Removed a workaround for some versions of Moonlight prior to mid-2022
- (UI) The PIN field is now cleared after successfully pairing
- (UI) User names are now treated as case-insensitive
- (Linux) Changed udev rule to automatically grant access to virtual input devices
- (UI) Several item descriptions were adjusted to reflect newer configuration recommendations
- (UI) Placeholder text opacity has been reduced to improve contrast with non-placeholder text
- (Video/Windows) Minor capture performance improvements

**Fixed**
- (Video) VRAM usage while streaming is significantly reduced, particularly with higher display resolutions and HDR
- (Network/UPnP) UPnP support was rewritten to fix several major issues handling router restarts, IP address changes, and port forwarding expiration
- (Input) Fixed modifier keys from the software keyboard on Android clients
- (Audio) Fixed handling of default audio device changes while streaming
- (Windows) Fixed streaming after Microsoft Remote Desktop or Fast User Switching has been used
- (Input) Fixed some games not recognizing emulated Guide button presses
- (Video/Windows) Fixed incorrect gamma when using an Advanced Color SDR display on the host
- (Installer/Windows) The installer is no longer blurry on High DPI systems
- (Systray/Windows) Fixed multiple system tray icons appearing if Sunshine exits unexpectedly
- (Systray/Windows) Fixed the system tray icon not appearing in several situations
- (Windows) Fixed hang on shutdown/restart if mDNS registration fails
- (UI) Fixed missing response headers
- (Stability) Fixed several possible crashes in cases where the client did not successfully connect
- (Stability) Fixed several memory leaks
- (Input/Windows) Back/Select input now correctly generates the Share button when emulating DS4 controllers
- (Audio/Windows) Fixed various bugs in audio-info.exe that led to inaccurate output on some systems
- (Video/Windows) Fixed incorrect resolution values from dxgi-info.exe on High DPI systems
- (Video/Linux) Fixed poor quality encoding from H.264 on Intel encoders
- (Config) Fixed a couple of typos in predefined resolutions

**Dependencies**
- Bump sphinx-copybutton from 0.5.1 to 0.5.2
- Bump sphinx from 6.13 to 7.0.1
- Bump third-party/nv-codec-headers from 2055784 to 2cd175b
- Bump furo from 2023.3.27 to 2023.5.20

**Misc**
- (Build/Linux) Add X11 to PLATFORM_LIBARIES when found
- (Build/macOS) Fix compilation with Clang 14
- (Docs) Fix nvlax URL
- (Docs) Add diagrams using graphviz
- (Docs) Improvements to source code documentation
- (Build) Unpin docker dependencies
- (Build/Linux) Honor install prefix for tray icon
- (Build/Windows) Unstripped binaries are now provided as a debuginfo package to support crash dump debugging
- (Config) Config directories are now created recursively

## [0.19.1] - 2023-03-30
**Fixed**
- (Audio) Fixed no audio issue introduced in v0.19.0

## [0.19.0] - 2023-03-29
**Breaking**
- (Linux/Flatpak) Moved Flatpak to org.freedesktop.Platform 22.08 and Cuda 12.0.0
  This will drop support for Nvidia GPUs with compute capability 3.5

**Added**
- (Input) Added option to suppress input from gamepads, keyboards, or mice
- (Input/Linux) Added unicode support for remote pasting (may not work on all DEs)
- (Input/Linux) Added XTest input fallback
- (UI) Added version notifications to web UI
- (Linux/Windows) Add system tray icon
- (Windows) Added ability to safely elevate commands that fail due to insufficient permissions when running as a service
- (Config) Added global prep commands, and ability to exclude an app from using global prep commands
- (Installer/Windows) Automatically install ViGEmBus if selected

**Changed**
- (Logging) Changed client verified messages to debug to prevent spamming the log
- (Config) Only save non default config values
- (Service/Linux) Use xdg-desktop-autostart for systemd service
- (Linux) Added config option to force capture method
- (Windows) Execute prep command in context of current user
- (Linux) Allow disconnected X11 outputs

**Fixed**
- (Input/Windows) Fix issue where internation keys were not translated correct, and modifier keys appeared stuck
- (Linux) Fixed startup when /dev/dri didn't exist
- (UI) Changes software encoding settings to select menu instead of text input
- (Initialization) Do not terminate upon failure, allowing access to the web UI

**Dependencies**
- Bump third-party/moonlight-common-c from 07beb0f to c9426a6
- Bump babel from 2.11.0 to 2.12.1
- Bump @fortawesome/fontawesome-free from 6.2.1 to 6.4.0
- Bump third-party/ViGEmClient from 9e842ba to 726404e
- Bump ffmpeg
- Bump third-party/miniupnp from 014c9df to e439318
- Bump furo from 2022.12.7 to 2023.3.27
- Bump third-party/nanors from 395e5ad to e9e242e

**Misc**
- (GitHub) Shared feature request board with Moonlight
- (Docs) Improved application examples
- (Docs) Added WIP documentation for source code using Doxygen and Breathe
- (Build) Fix linux clang build errors
- (Build/Archlinux) Skip irrelevant submodules
- (Build/Archlinux) Disable download timeout
- (Build/macOS) Support compiling for earlier releases of macOS
- (Docs) Add favicon
- (Docs) Add missing config default values
- (Build) Fix compiler warnings due to depreciated elements in C++17
- (Build) Fix libcurl link errors
- (Clang) Adjusted formatting rules

## [0.18.4] - 2023-02-20
**Fixed**
- (Linux/AUR) Drop support of AUR package
- (Docker) General enhancements to docker images

## [0.18.3] - 2023-02-13
**Added**
- (Linux) Added PKGBUILD for Archlinux based distros to releases
- (Linux) Added precompiled package for Archlinux based distros to releases
- (Docker) Added archlinux docker image (x86_64 only)

## [0.18.2] - 2023-02-13
**Fixed**
- (Video/KMV/Linux) Fixed wayland capture on Nvidia for KMS
- (Video/Linux) Implement vaSyncBuffer stuf for libva <2.9.0
- (UI) Fix issue where mime type was not being set for node_modules when using a reverse proxy
- (UI/macOS) Added missing audio sink config options
- (Linux) Specify correct Boost dependency versions
- (Video/AMF) Add missing encoder tunables

## [0.18.1] - 2023-01-31
**Fixed**
- (Linux) Fixed missing dependencies for deb and rpm packages
- (Linux) Use dynamic boost

## [0.18.0] - 2023-01-29
Attention, this release contains critical security fixes. Please update as soon as possible. Additionally, we are
encouraging users to change your Sunshine password, especially if you expose the web UI (i.e. port 47790 by default)
to the internet, or have ever uploaded your logs with verbose output to a public resource.

**Added**
- (Windows) Add support for Intel QuickSync
- (Linux) Added aarch64 deb and rpm packages
- (Windows) Add support for hybrid graphics systems, such as laptops with both integrated and discrete GPUs
- (Linux) Add support for streaming from Steam Deck Gaming Mode
- (Windows) Add HDR support, see https://docs.lizardbyte.dev/projects/sunshine/en/latest/about/usage.html#hdr-support

**Fixed**
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

**Breaking**
- (Apps) Removed automatic desktop entry (Re-add by adding an empty application named "Desktop" with no commands, "desktop.png" can be added as the image.)
- (Windows) Improved user upgrade experience (Suggest to manually uninstall existing Sunshine version before this upgrade. Do NOT select to remove everything, if prompted. Make a backup of config files before uninstall.)
- (Windows) Move config files to specific directory (files will be migrated automatically if using Windows installer)
- (Dependencies) Fix npm path (breaking change for package maintainers)

**Added**
- (macOS) Added initial support for arm64 on macOS through Macports portfile
- (Input) Added support for foreign keyboard input
- (Misc) Logs inside the WebUI and log to file
- (UI/Windows) Added an Apply button to configuration page when running as a service
- (Input/Windows) Enable Mouse Keys while streaming for systems with no physical mouse

**Fixed**
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

**Dependencies**
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
**Added**
- Add cover finder
- (Docker) Add arm64 docker image
- (Flatpak) Add installation helper scripts
- (Windows) Add support for Unicode input messages

**Fixed**
- (Linux) Reintroduce Ubuntu 20.04 and 22.04 specific deb packages
- (Linux) Fixed udev and systemd file locations

**Dependencies**
- Bump babel from 2.10.3 to 2.11.0
- Bump sphinx-copybutton from 0.5.0 to 0.5.1
- Bump KSXGitHub/github-actions-deploy-aur from 2.5.0 to 2.6.0
- Use npm for web dependencies (breaking change for third-party package maintainers)
- Update moonlight-common-c
- Use pre-built ffmpeg from LizardByte/build-deps for all sunshine builds (breaking change for third-party package maintainers)
- Bump furo from 2022.9.29 to 2022.12.7

**Misc**
- Misc org level workflow updates
- Fix misc typos in docs
- Fix winget release

## [0.15.0] - 2022-10-30
**Added**
- (Windows) Add firewall rules scripts
- (Windows) Automatically add and remove firewall rules at install/uninstall
- (Windows) Automatically add and remove service at install/uninstall
- (Docker) Official image added
- (Linux) Add aarch64 flatpak package

**Changed**
- (Windows/Linux/MacOS) - Move default config and apps file to assets directory
- (MacOS) Bump boost to 1.80 for macport builds
- (Linux) Remove backup and restore of config files

**Fixed**
- (Linux) - Create sunshine config directory if it doesn't exist
- (Linux) Remove portable home and config directories for AppImage
- (Windows) Include service install and uninstall scripts again
- (Windows) Automatically delete start menu entry upon uninstall
- (Windows) Automatically delete program install directory upon uninstall, with user prompt
- (Linux) Handle the case of no default audio sink
- (Windows/Linux/MacOS) Fix default image paths
- (Linux) Fix CUDA RGBA to NV12 conversion

## [0.14.1] - 2022-08-09
**Added**
- (Linux) Flatpak package added
- (Linux) AUR package automated updates
- (Windows) Winget package automated updates

**Changed**
- (General) Moved repo to @LizardByte GitHub org
- (WebUI) Fixed button spacing on home page
- (WebUI) Added Discord WidgetBot Crate

**Fixed**
- (Linux/Mac) Default config and app files now copied to user home directory
- (Windows) Default config and app files now copied to working directory

## [0.14.0] - 2022-06-15

**Added**
- (Documentation) Added Sphinx documentation available at https://sunshinestream.readthedocs.io/en/latest/
- (Development) Initial support for Localization
- (Linux) Add rpm package as release asset
- (macOS) Add Portfile as release asset
- (Windows) Add DwmFlush() call  to improve capture
- (Windows) Add Windows installer

**Fixed**
- (AMD) Fixed hwdevice being destroyed before context
- (Linux) Added missing dependencies to AppImage
- (Linux) Fixed rumble events causing game to freeze
- (Linux) Improved Pulse/Pipewire compatibility
- (Linux) Moved to single deb package
- (macOS) Fixed missing TPCircularBuffer submodule
- (Stream) Properly catch exceptions in stream broadcast handlers
- (Stream/Video) AVPacket fix

## [0.13.0] - 2022-02-27
**Added**
- (macOS) Initial support for macOS (#40)

## [0.12.0] - 2022-02-13
**Added**
- New command line argument `--version`
- Custom png poster support

**Changed**
- Correct software bitrate calculation
- Increase vbv-bufsize to 1/10 of requested bitrate
- Improvements to Web UI

## [0.11.1] - 2021-10-04
**Changed**
- (Linux) Fix search path for config file and assets

## [0.11.0] - 2021-10-04
**Added**
- (Linux) Added support for wlroots based compositors on Wayland.
- (Windows) Added an icon for the executable

**Changed**
- Fixed a bug causing segfault when connecting multiple controllers.
- (Linux) Improved NVENC, it now offloads converting images from RGB to NV12
- (Linux) Fixed a bug causes stuttering

## [0.10.1] - 2021-08-21
**Changed**
- (Linux) Re-enabled KMS

## [0.10.0] - 2021-08-20
**Added**
- Added support for Rumble with gamepads.
- Added support for keyboard shortcuts <--- See the README for details.
- (Windows) A very basic script has been added in Sunshine-Windows\tools <-- This will start Sunshine at boot with the highest privileges which is needed to display the login prompt.

**Changed**
- Some cosmetic changes to the WebUI.
- The first time the WebUI is opened, it will request the creation of a username/password pair from the user.
- Fixed audio crackling introduced in version 0.8.0
- (Linux) VAAPI hardware encoding now works on Intel i7-6700 at least. <-- For the best experience, using ffmpeg version 4.3 or higher is recommended.
- (Windows) Installing from debian package shouldn't overwrite your configuration files anymore. <-- It's recommended that you back up `/etc/sunshine/` before testing this.

## [0.9.0] - 2021-07-11
**Added**
- Added audio encryption
- (Linux) Added basic NVENC support on Linux
- (Windows) The Windows version can now capture the lock screen and the UAC prompt as long as it's run through `PsExec.exe` https://docs.microsoft.com/en-us/sysinternals/downloads/psexec

**Changed**
- Sunshine will now accept expired or not-yet-valid certificates, as long as they are signed properly.
- Fixed compatibility with iOS version of Moonlight
- Drastically reduced chance of being forced to skip error correction due to video frame size
- (Linux) sunshine.service will be installed automatically.

## [0.8.0] - 2021-06-30
**Added**
- Added mDNS support: Moonlight will automatically find Sunshine.
- Added UPnP support. It's off by default.

## [0.7.7] - 2021-06-24
**Added**
- (Linux) Added installation package for Debian

**Changed**
- Fixed incorrect scaling for absolute mouse coordinates when using multiple monitors.
- Fixed incorrect colors when scaling for software encoder

## [0.7.1] - 2021-06-18
**Changed**
- (Linux) Fixed an issue where it was impossible to start sunshine on ubuntu 20.04

## [0.7.0] - 2021-06-16
**Added**
- Added a Web Manager. Accessible through: https://localhost:47990 or https://<ip of your pc>:47990
- (Linux) Added hardware encoding support for AMD on Linux

**Changed**
- (Linux) Moved certificates and saved pairings generated during runtime to .config/sunshine on Linux

## [0.6.0] - 2021-05-26
**Added**
- Added support for surround audio

**Changed**
- Maintain aspect ratio when scaling video
- Fix issue where Sunshine is forced to drop frames when they are too large

## [0.5.0] - 2021-05-13
**Added**
- Added support for absolute mouse coordinates
- (Linux) Added support for streaming specific monitor on Linux
- (Windows) Added support for AMF on Windows

## [0.4.0] - 2020-05-03
**Changed**
- prep-cmd is now optional in apps.json
- Fixed bug causing video artifacts
- Fixed bug preventing Moonlight from closing app on exit
- Fixed bug causing preventing keyboard keys from repeating on latest version of Moonlight
- Fixed bug causing segfault when another session of sunshine was already running
- Fixed bug causing crash when monitor has resolution 1366x768

## [0.3.1] - 2020-04-24
**Changed**
- Fix a memory leak.

## [0.3.0] - 2020-04-23
**Changed**
- Hardware acceleration on NVidia GPU's for Video encoding on Windows

## [0.2.0] - 2020-03-21
**Changed**
- Multicasting is now supported: You can set the maximum simultaneous connections with the configurable option: channels
- Configuration variables can be overwritten on the command line: "name=value" --> it can be useful to set min_log_level=debug without modifying the configuration file
- Switches to make testing the pairing mechanism more convenient has been added, see "sunshine --help" for details

## [0.1.1] - 2020-01-30
**Added**
- (Linux) Added deb package and service for Linux

## [0.1.0] - 2020-01-27
**Added**
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
[0.18.4]: https://github.com/LizardByte/Sunshine/releases/tag/v0.18.4
[0.19.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.19.0
[0.19.1]: https://github.com/LizardByte/Sunshine/releases/tag/v0.19.1
[0.20.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.20.0
[0.21.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.21.0
[0.22.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.22.0
[0.22.1]: https://github.com/LizardByte/Sunshine/releases/tag/v0.22.1
[0.22.2]: https://github.com/LizardByte/Sunshine/releases/tag/v0.22.2
[0.23.0]: https://github.com/LizardByte/Sunshine/releases/tag/v0.23.0
