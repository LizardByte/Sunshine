Setup
=====
.. _latest release: https://github.com/LizardByte/Sunshine/releases/latest

The recommended method for running Sunshine is to use the `binaries`_ bundled with the `latest release`_.

Binaries
--------
Binaries of Sunshine are created for each release. They are available for Linux, macOS, and Windows.
Binaries can be found in the `latest release`_.

.. tip:: Some third party packages also exist. See
   :ref:`Third Party Packages <about/third_party_packages:third party packages>`.
   No support will be provided for third party packages!

Install
-------
.. tab:: Docker

   .. warning:: The Docker images are not recommended for most users. No support will be provided!

   Docker images are available on `Dockerhub.io <https://hub.docker.com/repository/docker/lizardbyte/sunshine>`__
   and `ghcr.io <https://github.com/orgs/LizardByte/packages?repo_name=sunshine>`__.

   See :ref:`Docker <about/docker:docker>` for additional information.

.. tab:: Linux

   **CUDA Compatibility**

   CUDA is used for NVFBC capture.

   .. tip:: See `CUDA GPUS <https://developer.nvidia.com/cuda-gpus>`__ to cross reference Compute Capability to your GPU.

   .. table::
      :widths: auto

      ===========================================  ==============   ==============    ================================
      Package                                      CUDA Version     Min Driver        CUDA Compute Capabilities
      ===========================================  ==============   ==============    ================================
      sunshine.AppImage                            11.8.0           450.80.02         35;50;52;60;61;62;70;75;80;86;90
      sunshine.pkg.tar.zst                         11.8.0           450.80.02         35;50;52;60;61;62;70;75;80;86;90
      sunshine_{arch}.flatpak                      12.0.0           525.60.13         50;52;60;61;62;70;75;80;86;90
      sunshine-debian-bookworm-{arch}.deb          12.0.0           525.60.13         50;52;60;61;62;70;75;80;86;90
      sunshine-fedora-39-{arch}.rpm                12.4.0           525.60.13         50;52;60;61;62;70;75;80;86;90
      sunshine-fedora-40-{arch}.rpm                n/a              n/a               n/a
      sunshine-ubuntu-22.04-{arch}.deb             11.8.0           450.80.02         35;50;52;60;61;62;70;75;80;86;90
      sunshine-ubuntu-24.04-{arch}.deb             11.8.0           450.80.02         35;50;52;60;61;62;70;75;80;86;90
      ===========================================  ==============   ==============    ================================

   .. tab:: AppImage

      .. caution:: Use distro-specific packages instead of the AppImage if they are available.

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

      #. Download ``sunshine.AppImage`` to your home directory.

         .. code-block:: bash

            cd ~
            wget https://github.com/LizardByte/Sunshine/releases/latest/download/sunshine.AppImage

      #. Open terminal and run the following code.

         .. code-block:: bash

            ./sunshine.AppImage --install

      Start:
         .. code-block:: bash

            ./sunshine.AppImage --install && ./sunshine.AppImage

      Uninstall:
         .. code-block:: bash

            ./sunshine.AppImage --remove

   .. tab:: Arch Linux Package

      .. warning:: We do not provide support for any AUR packages.

      .. tab:: Prebuilt Package

         #. Follow the instructions at LizardByte's `pacman-repo <https://github.com/LizardByte/pacman-repo>`__ to add
            the repository. Then run the following code.

            .. code-block:: bash

               pacman -S sunshine

         Uninstall:
            .. code-block:: bash

               pacman -R sunshine

      .. tab:: PKGBUILD Archive

         #. Open terminal and run the following code.

            .. code-block:: bash

               wget https://github.com/LizardByte/Sunshine/releases/latest/download/sunshine.pkg.tar.gz
               tar -xvf sunshine.pkg.tar.gz
               cd sunshine

               # install optional dependencies
               pacman -S cuda  # Nvidia GPU encoding support
               pacman -S libva-mesa-driver  # AMD GPU encoding support

               makepkg -si

         Uninstall:
            .. code-block:: bash

               pacman -R sunshine

   .. tab:: Debian/Ubuntu Package

      #. Download ``sunshine-{distro}-{distro-version}-{arch}.deb`` and run the following code.

         .. code-block:: bash

            sudo dpkg -i ./sunshine-{distro}-{distro-version}-{arch}.deb

         .. note:: The ``{distro-version}`` is the version of the distro we built the package on. The ``{arch}`` is the
            architecture of your operating system.

         .. tip:: You can double click the deb file to see details about the package and begin installation.

      Uninstall:
         .. code-block:: bash

            sudo apt remove sunshine

   .. tab:: Flatpak Package

      .. caution:: Use distro-specific packages instead of the Flatpak if they are available.

      .. important:: The instructions provided here are for the version supplied in the `latest release`_, which does
         not necessarily match the version in the Flathub repository!

      #. Install `Flatpak <https://flatpak.org/setup/>`__ as required.
      #. Download ``sunshine_{arch}.flatpak`` and run the following code.

         .. note:: Be sure to replace ``{arch}`` with the architecture for your operating system.

         System level (recommended)
            .. code-block:: bash

               flatpak install --system ./sunshine_{arch}.flatpak

         User level
            .. code-block:: bash

               flatpak install --user ./sunshine_{arch}.flatpak

         Additional installation (required)
            .. code-block:: bash

               flatpak run --command=additional-install.sh dev.lizardbyte.app.Sunshine

      Start:
         X11 and NVFBC capture (X11 Only)
            .. code-block:: bash

               flatpak run dev.lizardbyte.app.Sunshine

         KMS capture (Wayland & X11)
            .. code-block:: bash

               sudo -i PULSE_SERVER=unix:$(pactl info | awk '/Server String/{print$3}') \
                 flatpak run dev.lizardbyte.app.Sunshine

      Uninstall:
         .. code-block:: bash

            flatpak run --command=remove-additional-install.sh dev.lizardbyte.app.Sunshine
            flatpak uninstall --delete-data dev.lizardbyte.app.Sunshine

   .. tab:: Homebrew

      .. important:: The Homebrew package is experimental.

      #. Install `Homebrew <https://docs.brew.sh/Installation>`__
      #. Update the Homebrew sources and install Sunshine.

         .. code-block:: bash

            brew tap LizardByte/homebrew
            brew install sunshine

   .. tab:: RPM Package

      #. Add `rpmfusion` repositories by running the following code.

         .. code-block:: bash

            sudo dnf install \
              https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
              https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm

      #. Download ``sunshine-{distro}-{distro-version}-{arch}.rpm`` and run the following code.

         .. code-block:: bash

            sudo dnf install ./sunshine-{distro}-{distro-version}-{arch}.rpm

         .. note:: The ``{distro-version}`` is the version of the distro we built the package on. The ``{arch}`` is the
            architecture of your operating system.

         .. tip:: You can double click the rpm file to see details about the package and begin installation.

      Uninstall:
         .. code-block:: bash

            sudo dnf remove sunshine

   The `deb`, `rpm`, `zst`, `Flatpak` and `AppImage` packages should handle the steps below automatically.
   Third party packages may not.

   Sunshine needs access to `uinput` to create mouse and gamepad virtual devices and (optionally) to `uhid`
   in order to emulate a PS5 DualSense joypad with Gyro, Acceleration and Touchpad support.

   #. Create and reload `udev` rules for `uinput` and `uhid`.
         .. code-block:: bash

            echo 'KERNEL=="uinput", SUBSYSTEM=="misc", OPTIONS+="static_node=uinput", TAG+="uaccess"\nKERNEL=="uhid", TAG+="uaccess"' | \
            sudo tee /etc/udev/rules.d/60-sunshine.rules
            sudo udevadm control --reload-rules
            sudo udevadm trigger
            sudo modprobe uinput

   #. Enable permissions for KMS capture.
         .. warning:: Capture of most Wayland-based desktop environments will fail unless this step is performed.

         .. note:: ``cap_sys_admin`` may as well be root, except you don't need to be root to run it. It is necessary to
            allow Sunshine to use KMS capture.

         **Enable**
            .. code-block:: bash

               sudo setcap cap_sys_admin+p $(readlink -f $(which sunshine))

         **Disable (for Xorg/X11 only)**
            .. code-block:: bash

               sudo setcap -r $(readlink -f $(which sunshine))

   #. Optionally, configure autostart service

      - filename: ``~/.config/systemd/user/sunshine.service``
      - contents:
            .. code-block:: cfg

               [Unit]
               Description=Sunshine self-hosted game stream host for Moonlight.
               StartLimitIntervalSec=500
               StartLimitBurst=5

               [Service]
               ExecStart=<see table>
               Restart=on-failure
               RestartSec=5s
               #Flatpak Only
               #ExecStop=flatpak kill dev.lizardbyte.app.Sunshine

               [Install]
               WantedBy=graphical-session.target

            .. table::
               :widths: auto

               =========   ==============================================   ===============
               package     ExecStart                                        Auto Configured
               =========   ==============================================   ===============
               ArchLinux   /usr/bin/sunshine                                ✔
               deb         /usr/bin/sunshine                                ✔
               rpm         /usr/bin/sunshine                                ✔
               AppImage    ~/sunshine.AppImage                              ✔
               Flatpak     flatpak run dev.lizardbyte.app.Sunshine          ✔
               =========   ==============================================   ===============

      **Start once**
            .. code-block:: bash

               systemctl --user start sunshine

      **Start on boot**
            .. code-block:: bash

               systemctl --user enable sunshine

   #. Reboot
         .. code-block:: bash

            sudo reboot now

.. tab:: macOS

   .. important:: Sunshine on macOS is experimental. Gamepads do not work.

   .. tab:: Homebrew

      #. Install `Homebrew <https://docs.brew.sh/Installation>`__
      #. Update the Homebrew sources and install Sunshine.

         .. code-block:: bash

            brew tap LizardByte/homebrew
            brew install sunshine

   .. tab:: Portfile

      #. Install `MacPorts <https://www.macports.org>`__
      #. Update the Macports sources.

         .. code-block:: bash

            sudo nano /opt/local/etc/macports/sources.conf

         Add this line, replacing your username, below the line that starts with ``rsync``.
            ``file:///Users/<username>/ports``

         ``Ctrl+x``, then ``Y`` to exit and save changes.

      #. Download and install by running the following code.

         .. code-block:: bash

            mkdir -p ~/ports/multimedia/sunshine
            cd ~/ports/multimedia/sunshine
            curl -OL https://github.com/LizardByte/Sunshine/releases/latest/download/Portfile
            cd ~/ports
            portindex
            sudo port install sunshine

      #. The first time you start Sunshine, you will be asked to grant access to screen recording and your microphone.

      #. Optionally, install service

         .. code-block:: bash

            sudo port load Sunshine

      Uninstall:
         .. code-block:: bash

            sudo port uninstall sunshine

   Sunshine can only access microphones on macOS due to system limitations. To stream system audio use
   `Soundflower <https://github.com/mattingalls/Soundflower>`__ or
   `BlackHole <https://github.com/ExistentialAudio/BlackHole>`__.

   .. note:: Command Keys are not forwarded by Moonlight. Right Option-Key is mapped to CMD-Key.

   .. caution:: Gamepads are not currently supported.

.. tab:: Windows

   .. tab:: Installer

      #. Download and install ``sunshine-windows-installer.exe``

      .. attention:: You should carefully select or unselect the options you want to install. Do not blindly install or
         enable features.

      To uninstall, find Sunshine in the list `here <ms-settings:installed-apps>`__ and select "Uninstall" from the
      overflow menu. Different versions of Windows may provide slightly different steps for uninstall.

   .. tab:: Standalone

      .. warning:: By using this package instead of the installer, performance will be reduced. This package is not
         recommended for most users. No support will be provided!

      #. Download and extract ``sunshine-windows-portable.zip``
      #. Open command prompt as administrator
      #. Firewall rules

         Install:
            .. code-block:: bash

               cd /d {path to extracted directory}
               scripts/add-firewall-rule.bat

         Uninstall:
            .. code-block:: bash

               cd /d {path to extracted directory}
               scripts/delete-firewall-rule.bat

      #. Virtual Gamepad Support

         Install:
            .. code-block:: bash

               cd /d {path to extracted directory}
               scripts/install-gamepad.bat

         Uninstall:
            .. code-block:: bash

               cd /d {path to extracted directory}
               scripts/uninstall-gamepad.bat

      #. Windows service

         Install:
            .. code-block:: bash

               cd /d {path to extracted directory}
               scripts/install-service.bat
               scripts/autostart-service.bat

         Uninstall:
            .. code-block:: bash

               cd /d {path to extracted directory}
               scripts/uninstall-service.bat

      To uninstall, delete the extracted directory which contains the ``sunshine.exe`` file.

Usage
-----
#. If Sunshine is not installed/running as a service, then start sunshine with the following command, unless a start
   command is listed in the specified package `install`_ instructions above.

   .. note:: A service is a process that runs in the background. This is the default when installing Sunshine from the
      Windows installer. Running multiple instances of Sunshine is not advised.

   **Basic usage**
      .. code-block:: bash

         sunshine

   **Specify config file**
      .. code-block:: bash

         sunshine <directory of conf file>/sunshine.conf

      .. note:: You do not need to specify a config file.
         If no config file is entered the default location will be used.

      .. attention:: The configuration file specified will be created if it doesn't exist.

   **Start Sunshine over SSH (Linux/X11)**
      Assuming you are already logged into the host, you can use this command

      .. code-block:: bash

         ssh <user>@<ip_address> 'export DISPLAY=:0; sunshine'

      If you are logged into the host with only a tty (teletypewriter), you can use ``startx`` to start the
      X server prior to executing sunshine.
      You nay need to add ``sleep`` between ``startx`` and ``sunshine`` to allow more time for the display to be ready.

      .. code-block:: bash

         ssh <user>@<ip_address> 'startx &; export DISPLAY=:0; sunshine'

      .. tip:: You could also utilize the ``~/.bash_profile`` or ``~/.bashrc`` files to setup the ``DISPLAY``
         variable.

      .. seealso::

         See :ref:`Remote SSH Headless Setup
         <about/guides/linux/headless_ssh:Remote SSH Headless Setup>` on
         how to setup a headless streaming server without autologin and dummy plugs (X11 + NVidia GPUs)

#. Configure Sunshine in the web ui

   The web ui is available on `https://localhost:47990 <https://localhost:47990>`__ by default. You may replace
   `localhost` with your internal ip address.

   .. attention:: Ignore any warning given by your browser about "insecure website". This is due to the SSL certificate
      being self signed.

   .. caution:: If running for the first time, make sure to note the username and password that you created.

    #. Add games and applications.
    #. Adjust any configuration settings as needed.

#. In Moonlight, you may need to add the PC manually.
#. When Moonlight requests for you insert the pin:

   - Login to the web ui
   - Go to "PIN" in the Navbar
   - Type in your PIN and press Enter, you should get a Success Message
   - In Moonlight, select one of the Applications listed

Network
-------
The Sunshine user interface will be available on port 47990 by default.

.. warning:: Exposing ports to the internet can be dangerous. Do this at your own risk.

Arguments
---------
To get a list of available arguments run the following:

.. tab:: General

   .. code-block:: bash

      sunshine --help

.. tab:: AppImage

   .. code-block:: bash

      ./sunshine.AppImage --help

.. tab:: Flatpak

   .. code-block:: bash

      flatpak run --command=sunshine dev.lizardbyte.app.Sunshine --help

Shortcuts
---------
All shortcuts start with ``CTRL + ALT + SHIFT``, just like Moonlight

- ``CTRL + ALT + SHIFT + N`` - Hide/Unhide the cursor (This may be useful for Remote Desktop Mode for Moonlight)
- ``CTRL + ALT + SHIFT + F1/F12`` - Switch to different monitor for Streaming

Application List
----------------
- Applications should be configured via the web UI.
- A basic understanding of working directories and commands is required.
- You can use Environment variables in place of values
- ``$(HOME)`` will be replaced by the value of ``$HOME``
- ``$$`` will be replaced by ``$``, e.g. ``$$(HOME)`` will be become ``$(HOME)``
- ``env`` - Adds or overwrites Environment variables for the commands/applications run by Sunshine
- ``"Variable name":"Variable value"``
- ``apps`` - The list of applications
- Advanced users may want to edit the application list manually. The format is ``json``.
- Example ``json`` application:
   .. code-block:: json

      {
          "cmd": "command to open app",
          "detached": [
              "some-command",
              "another-command"
          ],
          "image-path": "/full-path/to/png-image",
          "name": "An App",
          "output": "/full-path/to/command-log-file",
          "prep-cmd": [
              {
                  "do": "some-command",
                  "undo": "undo-that-command"
              }
          ],
          "working-dir": "/full-path/to/working-directory"
      }

   - ``cmd`` - The main application
   - ``detached`` - A list of commands to be run and forgotten about

     - If not specified, a process is started that sleeps indefinitely

   - ``image-path`` - The full path to the cover art image to use.
   - ``name`` - The name of the application/game
   - ``output`` - The file where the output of the command is stored
   - ``auto-detach`` - Specifies whether the app should be treated as detached if it exits quickly
   - ``wait-all`` - Specifies whether to wait for all processes to terminate rather than just the initial process
   - ``exit-timeout`` - Specifies how long to wait in seconds for the process to gracefully exit (default: 5 seconds)
   - ``prep-cmd`` - A list of commands to be run before/after the application

     - If any of the prep-commands fail, starting the application is aborted
     - ``do`` - Run before the application

       - If it fails, all ``undo`` commands of the previously succeeded ``do`` commands are run

     - ``undo`` - Run after the application has terminated

       - Failures of ``undo`` commands are ignored

   - ``working-dir`` - The working directory to use. If not specified, Sunshine will use the application directory.

- For more examples see :ref:`app examples <about/guides/app_examples:app examples>`.

Considerations
--------------
- On Windows, Sunshine uses the Desktop Duplication API which only supports capturing from the GPU used for display.
  If you want to capture and encode on the eGPU, connect a display or HDMI dummy display dongle to it and run the games
  on that display.
- When an application is started, if there is an application already running, it will be terminated.
- When the application has been shutdown, the stream shuts down as well.

  - For example, if you attempt to run ``steam`` as a ``cmd`` instead of ``detached`` the stream will immediately fail.
    This is due to the method in which the steam process is executed. Other applications may behave similarly.
  - This does not apply to ``detached`` applications.

- The "Desktop" app works the same as any other application except it has no commands. It does not start an application,
  instead it simply starts a stream. If you removed it and would like to get it back, just add a new application with
  the name "Desktop" and "desktop.png" as the image path.
- For the Linux flatpak you must prepend commands with ``flatpak-spawn --host``.

HDR Support
-----------
Streaming HDR content is officially supported on Windows hosts and experimentally supported for Linux hosts.

- General HDR support information and requirements:

  - HDR must be activated in the host OS, which may require an HDR-capable display or EDID emulator dongle connected to your host PC.
  - You must also enable the HDR option in your Moonlight client settings, otherwise the stream will be SDR (and probably overexposed if your host is HDR).
  - A good HDR experience relies on proper HDR display calibration both in the OS and in game. HDR calibration can differ significantly between client and host displays.
  - You may also need to tune the brightness slider or HDR calibration options in game to the different HDR brightness capabilities of your client's display.
  - Some GPUs video encoders can produce lower image quality or encoding performance when streaming in HDR compared to SDR.

- Additional information:

.. tab:: Windows

     - HDR streaming is supported for Intel, AMD, and NVIDIA GPUs that support encoding HEVC Main 10 or AV1 10-bit profiles.
     - We recommend calibrating the display by streaming the Windows HDR Calibration app to your client device and saving an HDR calibration profile to use while streaming.
     - Older games that use NVIDIA-specific NVAPI HDR rather than native Windows HDR support may not display properly in HDR.

.. tab:: Linux

     - HDR streaming is supported for Intel and AMD GPUs that support encoding HEVC Main 10 or AV1 10-bit profiles using VAAPI.
     - The KMS capture backend is required for HDR capture. Other capture methods, like NvFBC or X11, do not support HDR.
     - You will need a desktop environment with a compositor that supports HDR rendering, such as Gamescope or KDE Plasma 6.

.. seealso::
   `Arch wiki on HDR Support for Linux <https://wiki.archlinux.org/title/HDR_monitor_support>`__ and
   `Reddit Guide for HDR Support for AMD GPUs
   <https://www.reddit.com/r/linux_gaming/comments/10m2gyx/guide_alpha_test_hdr_on_linux>`__

Tutorials and Guides
--------------------
Tutorial videos are available `here <https://www.youtube.com/playlist?list=PLMYr5_xSeuXAbhxYHz86hA1eCDugoxXY0>`_.

Guides are available :doc:`here <./guides/guides>`.

.. admonition:: Community!

   Tutorials and Guides are community generated. Want to contribute? Reach out to us on our discord server.
