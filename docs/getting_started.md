# Getting Started

The recommended method for running Sunshine is to use the [binaries](#binaries) included in the
[latest release][latest-release], unless otherwise specified.

[Pre-releases](https://github.com/LizardByte/Sunshine/releases) are also available. These should be considered beta,
and release artifacts may be missing when merging changes on a faster cadence.

## Binaries

Binaries of Sunshine are created for each release. They are available for Linux, macOS, and Windows.
Binaries can be found in the [latest release][latest-release].

@tip{Some third party packages also exist.
See [Third Party Packages](third_party_packages.md) for more information.
No support will be provided for third party packages!}

## Install

### Docker
@warning{The Docker images are not recommended for most users.}

Docker images are available on [Dockerhub.io](https://hub.docker.com/repository/docker/lizardbyte/sunshin)
and [ghcr.io](https://github.com/orgs/LizardByte/packages?repo_name=sunshine).

See [Docker](../DOCKER_README.md) for more information.

### Linux
**CUDA Compatibility**

CUDA is used for NVFBC capture.

@tip{See [CUDA GPUS](https://developer.nvidia.com/cuda-gpus) to cross-reference Compute Capability to your GPU.}

<table>
    <caption>CUDA Compatibility</caption>
    <tr>
        <th>CUDA Version</th>
        <th>Min Driver</th>
        <th>CUDA Compute Capabilities</th>
        <th>Package</th>
    </tr>
    <tr>
        <td rowspan="3">11.8.0</td>
        <td rowspan="3">450.80.02</td>
        <td rowspan="3">35;50;52;60;61;62;70;72;75;80;86;87;89;90</td>
        <td>sunshine.AppImage</td>
    </tr>
    <tr>
        <td>sunshine-ubuntu-22.04-{arch}.deb</td>
    </tr>
    <tr>
        <td>sunshine-ubuntu-24.04-{arch}.deb</td>
    </tr>
    <tr>
        <td rowspan="1">12.0.0</td>
        <td rowspan="2">525.60.13</td>
        <td rowspan="4">50;52;60;61;62;70;72;75;80;86;87;89;90</td>
        <td>sunshine-debian-bookworm-{arch}.deb</td>
    </tr>
    <tr>
        <td rowspan="1">12.5.1</td>
        <td>sunshine.pkg.tar.zst</td>
    </tr>
    <tr>
        <td rowspan="2">12.6.2</td>
        <td rowspan="2">560.35.03</td>
        <td>sunshine_{arch}.flatpak</td>
    </tr>
    <tr>
        <td>Sunshine (copr)</td>
    </tr>
</table>

#### AppImage
@caution{Use distro-specific packages instead of the AppImage if they are available.}

According to AppImageLint the supported distro matrix of the AppImage is below.

- ✖ Debian bullseye
- ✔ Debian bookworm
- ✔ Debian trixie
- ✔ Debian sid
- ✔ Ubuntu noble
- ✔ Ubuntu jammy
- ✖ Ubuntu focal
- ✖ Ubuntu bionic
- ✖ Ubuntu xenial
- ✖ Ubuntu trusty
- ✖ CentOS 7

##### Install
1. Download [sunshine.AppImage](https://github.com/LizardByte/Sunshine/releases/latest/download/sunshine.AppImage)
   into your home directory.
   ```bash
   cd ~
   wget https://github.com/LizardByte/Sunshine/releases/latest/download/sunshine.AppImage
   ```
2. Open terminal and run the following command.
   ```bash
   ./sunshine.AppImage --install
   ```

##### Run
```bash
./sunshine.AppImage --install && ./sunshine.AppImage
```

##### Uninstall
```bash
./sunshine.AppImage --remove
```

#### ArchLinux
@warning{We do not provide support for any AUR packages.}

##### Install Prebuilt Packages
Follow the instructions at LizardByte's [pacman-repo](https://github.com/LizardByte/pacman-repo) to add
the repository. Then run the following command.
```bash
pacman -S sunshine
```

##### Install PKGBUILD Archive
Open terminal and run the following command.
```bash
wget https://github.com/LizardByte/Sunshine/releases/latest/download/sunshine.pkg.tar.gz
tar -xvf sunshine.pkg.tar.gz
cd sunshine

# install optional dependencies
pacman -S cuda  # Nvidia GPU encoding support
pacman -S libva-mesa-driver  # AMD GPU encoding support

makepkg -si
```

##### Uninstall
```bash
pacman -R sunshine
```

#### Debian/Ubuntu
##### Install
Download `sunshine-{distro}-{distro-version}-{arch}.deb` and run the following command.
```bash
sudo dpkg -i ./sunshine-{distro}-{distro-version}-{arch}.deb
```

@note{The `{distro-version}` is the version of the distro we built the package on. The `{arch}` is the
architecture of your operating system.}

@tip{You can double-click the deb file to see details about the package and begin installation.}

##### Uninstall
```bash
sudo apt remove sunshine
```

#### Fedora
@tip{The package name is case-sensitive.}

##### Install
1. Enable copr repository.
   ```bash
   sudo dnf copr enable lizardbyte/stable
   ```

   or
   ```bash
   sudo dnf copr enable lizardbyte/beta
   ```

2. Install the package.
   ```bash
   sudo dnf install Sunshine
   ```

##### Uninstall
```bash
sudo dnf remove Sunshine
```

#### Flatpak
@caution{Use distro-specific packages instead of the Flatpak if they are available.}

Using this package requires that you have [Flatpak](https://flatpak.org/setup) installed.

##### Download (local option)
1. Download `sunshine_{arch}.flatpak` and run the following command.
   @note{Replace `{arch}` with your system architecture.}

##### Install (system level)
**Flathub**
```bash
flatpak install --system flathub dev.lizardbyte.app.Sunshine
```

**Local**
```bash
flatpak install --system ./sunshine_{arch}.flatpak
```

##### Install (user level)
**Flathub**
```bash
flatpak install --user flathub dev.lizardbyte.app.Sunshine
```

**Local**
```bash
flatpak install --user ./sunshine_{arch}.flatpak
```

##### Additional installation (required)
```bash
flatpak run --command=additional-install.sh dev.lizardbyte.app.Sunshine
```

##### Run with NVFBC capture (X11 Only)
```bash
flatpak run dev.lizardbyte.app.Sunshine
```

##### Run with KMS capture (Wayland & X11)
```bash
sudo -i PULSE_SERVER=unix:/run/user/$(id -u $whoami)/pulse/native flatpak run dev.lizardbyte.app.Sunshine
```

##### Uninstall
```bash
flatpak run --command=remove-additional-install.sh dev.lizardbyte.app.Sunshine
flatpak uninstall --delete-data dev.lizardbyte.app.Sunshine
```

#### Homebrew
@important{The Homebrew package is experimental on Linux.}

This package requires that you have [Homebrew](https://docs.brew.sh/Installation) installed.

##### Install
```bash
brew tap LizardByte/homebrew
brew install sunshine
```

##### Uninstall
```bash
brew uninstall sunshine
```

### macOS

@important{Sunshine on macOS is experimental. Gamepads do not work.}

#### Homebrew
This package requires that you have [Homebrew](https://docs.brew.sh/Installation) installed.

##### Install
```bash
brew tap LizardByte/homebrew
brew install sunshine
```

##### Uninstall
```bash
brew uninstall sunshine
```

@tip{For beta you can replace `sunshine` with `sunshine-beta` in the above commands.}

#### Portfile
This package requires that you have [MacPorts](https://www.macports.org/install.php) installed.

##### Install
1. Update the Macports sources.
   ```bash
   sudo nano /opt/local/etc/macports/sources.conf
   ```

   Add this line, replacing your username, below the line that starts with `rsync`.
   ```bash
   file:///Users/<username>/ports
   ```

   `Ctrl+x`, then `Y` to exit and save changes.

2. Download and install by running the following commands.
   ```bash
   mkdir -p ~/ports/multimedia/sunshine
   cd ~/ports/multimedia/sunshine
   curl -OL https://github.com/LizardByte/Sunshine/releases/latest/download/Portfile
   cd ~/ports
   portindex
   sudo port install sunshine
   ```

##### Install service (optional)
```bash
sudo port load sunshine
```

##### Uninstall
```bash
sudo port uninstall sunshine
```

### Windows

#### Installer (recommended)

1. Download and install
   [sunshine-windows-installer.exe](https://github.com/LizardByte/Sunshine/releases/latest/download/sunshine-windows-installer.exe)

@attention{You should carefully select or unselect the options you want to install. Do not blindly install or
enable features.}

To uninstall, find Sunshine in the list <a href="ms-settings:installed-apps">here</a> and select "Uninstall" from the
overflow menu. Different versions of Windows may provide slightly different steps for uninstall.

#### Standalone (lite version)

@warning{By using this package instead of the installer, performance will be reduced. This package is not
recommended for most users. No support will be provided!}

1. Download and extract
   [sunshine-windows-portable.zip](https://github.com/LizardByte/Sunshine/releases/latest/download/sunshine-windows-portable.zip)
2. Open command prompt as administrator
3. Firewall rules

   Install:
   ```bash
   cd /d {path to extracted directory}
   scripts/add-firewall-rule.bat
   ```

   Uninstall:
   ```bash
   cd /d {path to extracted directory}
   scripts/delete-firewall-rule.bat
   ```

4. Virtual Gamepad Support

   Install:
   ```bash
   cd /d {path to extracted directory}
   scripts/install-gamepad.bat
   ```

   Uninstall:
   ```bash
   cd /d {path to extracted directory}
   scripts/uninstall-gamepad.bat
   ```

5. Windows service

   Install:
   ```bash
   cd /d {path to extracted directory}
   scripts/install-service.bat
   scripts/autostart-service.bat
   ```

   Uninstall:
   ```bash
   cd /d {path to extracted directory}
   scripts/uninstall-service.bat
   ```

To uninstall, delete the extracted directory which contains the `sunshine.exe` file.

## Initial Setup
After installation, some initial setup is required.

### Linux

#### KMS Capture
@warning{Capture of most Wayland-based desktop environments will fail unless this step is performed.}
@note{`cap_sys_admin` may as well be root, except you don't need to be root to run the program. This is necessary to
allow Sunshine to use KMS capture.}

##### Enable
```bash
sudo setcap cap_sys_admin+p $(readlink -f $(which sunshine))
```

#### X11 Capture
For X11 capture to work, you may need to disable the capabilities that were set for KMS capture.

```bash
sudo setcap -r $(readlink -f $(which sunshine))
```

#### Service

**Start once**
```bash
systemctl --user start sunshine
```

**Start on boot**
```bash
systemctl --user enable sunshine
```

### macOS
The first time you start Sunshine, you will be asked to grant access to screen recording and your microphone.

Sunshine can only access microphones on macOS due to system limitations. To stream system audio use
[Soundflower](https://github.com/mattingalls/Soundflower) or
[BlackHole](https://github.com/ExistentialAudio/BlackHole).

@note{Command Keys are not forwarded by Moonlight. Right Option-Key is mapped to CMD-Key.}
@caution{Gamepads are not currently supported.}

## Usage

### Basic usage
If Sunshine is not installed/running as a service, then start Sunshine with the following command, unless a start
command is listed in the specified package [install](#install) instructions above.

@note{A service is a process that runs in the background. This is the default when installing Sunshine from the
Windows installer. Running multiple instances of Sunshine is not advised.}

```bash
sunshine
```

### Specify config file
```bash
sunshine <directory of conf file>/sunshine.conf
```

@note{You do not need to specify a config file. If no config file is entered the default location will be used.}

@attention{The configuration file specified will be created if it doesn't exist.}

### Start Sunshine over SSH (Linux/X11)
Assuming you are already logged into the host, you can use this command

```bash
ssh <user>@<ip_address> 'export DISPLAY=:0; sunshine'
```

If you are logged into the host with only a tty (teletypewriter), you can use `startx` to start the X server prior to
executing Sunshine. You nay need to add `sleep` between `startx` and `sunshine` to allow more time for the display to
be ready.

```bash
ssh <user>@<ip_address> 'startx &; export DISPLAY=:0; sunshine'
```

@tip{You could also utilize the `~/.bash_profile` or `~/.bashrc` files to set up the `DISPLAY` variable.}

@seealso{ See [Remote SSH Headless Setup](md_docs_2guides.html#remote-ssh-headless-setup)
on how to set up a headless streaming server without autologin and dummy plugs (X11 + NVidia GPUs)}

### Configuration

Sunshine is configured via the web ui, which is available on [https://localhost:47990](https://localhost:47990)
by default. You may replace *localhost* with your internal ip address.

@attention{Ignore any warning given by your browser about "insecure website". This is due to the SSL certificate
being self-signed.}

@caution{If running for the first time, make sure to note the username and password that you created.}

1. Add games and applications.
2. Adjust any configuration settings as needed.
3. In Moonlight, you may need to add the PC manually.
4. When Moonlight requests for you insert the pin:

   - Login to the web ui
   - Go to "PIN" in the Navbar
   - Type in your PIN and press Enter, you should get a Success Message
   - In Moonlight, select one of the Applications listed

### Arguments
To get a list of available arguments, run the following command.

@tabs{
   @tab{ General | @code{.bash}
      sunshine --help
      @endcode }
   @tab{ AppImage | @code{.bash}
      ./sunshine.AppImage --help
      @endcode }
   @tab{ Flatpak | @code{.bash}
      flatpak run --command=sunshine dev.lizardbyte.app.Sunshine --help
      @endcode }
}

### Shortcuts
All shortcuts start with `Ctrl+Alt+Shift`, just like Moonlight.

* `Ctrl+Alt+Shift+N`: Hide/Unhide the cursor (This may be useful for Remote Desktop Mode for Moonlight)
* `Ctrl+Alt+Shift+F1/F12`: Switch to different monitor for Streaming

### Application List
* Applications should be configured via the web UI
* A basic understanding of working directories and commands is required
* You can use Environment variables in place of values
* `$(HOME)` will be replaced by the value of `$HOME`
* `$$` will be replaced by `$`, e.g. `$$(HOME)` will be become `$(HOME)`
* `env` - Adds or overwrites Environment variables for the commands/applications run by Sunshine.
  This can only be changed by modifying the `apps.json` file directly.

### Considerations
* On Windows, Sunshine uses the Desktop Duplication API which only supports capturing from the GPU used for display.
  If you want to capture and encode on the eGPU, connect a display or HDMI dummy display dongle to it and run the games
  on that display.
* When an application is started, if there is an application already running, it will be terminated.
* If any of the prep-commands fail, starting the application is aborted.
* When the application has been shutdown, the stream shuts down as well.

  * For example, if you attempt to run `steam` as a `cmd` instead of `detached` the stream will immediately fail.
    This is due to the method in which the steam process is executed. Other applications may behave similarly.
  * This does not apply to `detached` applications.

* The "Desktop" app works the same as any other application except it has no commands. It does not start an application,
  instead it simply starts a stream. If you removed it and would like to get it back, just add a new application with
  the name "Desktop" and "desktop.png" as the image path.
* For the Linux flatpak you must prepend commands with `flatpak-spawn --host`.

### HDR Support
Streaming HDR content is officially supported on Windows hosts and experimentally supported for Linux hosts.

* General HDR support information and requirements:

  * HDR must be activated in the host OS, which may require an HDR-capable display or EDID emulator dongle
    connected to your host PC.
  * You must also enable the HDR option in your Moonlight client settings, otherwise the stream will be SDR
    (and probably overexposed if your host is HDR).
  * A good HDR experience relies on proper HDR display calibration both in the OS and in game. HDR calibration can
    differ significantly between client and host displays.
  * You may also need to tune the brightness slider or HDR calibration options in game to the different HDR brightness
    capabilities of your client's display.
  * Some GPUs video encoders can produce lower image quality or encoding performance when streaming in HDR compared
    to SDR.

* Additional information:

  @tabs{
    @tab{ Windows |
    - HDR streaming is supported for Intel, AMD, and NVIDIA GPUs that support encoding HEVC Main 10 or AV1 10-bit profiles.
    - We recommend calibrating the display by streaming the Windows HDR Calibration app to your client device and saving an HDR calibration profile to use while streaming.
    - Older games that use NVIDIA-specific NVAPI HDR rather than native Windows HDR support may not display properly in HDR.
    }

    @tab{ Linux |
    - HDR streaming is supported for Intel and AMD GPUs that support encoding HEVC Main 10 or AV1 10-bit profiles using VAAPI.
    - The KMS capture backend is required for HDR capture. Other capture methods, like NvFBC or X11, do not support HDR.
    - You will need a desktop environment with a compositor that supports HDR rendering, such as Gamescope or KDE Plasma 6.

    @seealso{[Arch wiki on HDR Support for Linux](https://wiki.archlinux.org/title/HDR_monitor_support) and
    [Reddit Guide for HDR Support for AMD GPUs](https://www.reddit.com/r/linux_gaming/comments/10m2gyx/guide_alpha_test_hdr_on_linux)}
    }
  }

### Tutorials and Guides
Tutorial videos are available [here](https://www.youtube.com/playlist?list=PLMYr5_xSeuXAbhxYHz86hA1eCDugoxXY0).

Guides are available [here](guides.md).

@admonition{Community! |
Tutorials and Guides are community generated. Want to contribute? Reach out to us on our discord server.}

<div class="section_buttons">

| Previous                 |                      Next |
|:-------------------------|--------------------------:|
| [Overview](../README.md) | [Changelog](changelog.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>

[latest-release]: https://github.com/LizardByte/Sunshine/releases/latest
