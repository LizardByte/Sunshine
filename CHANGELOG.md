# Changelog

## [0.13.0] - 2022-02-27
### Added
- (MacOS) Initial support for MacOS (#40)

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
