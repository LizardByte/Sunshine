Usage
=====
#. See the `setup`_ section for your specific OS.
#. If you did not install the service, then start sunshine with the following command, unless a start command is listed
   in the specified package :ref:`installation <about/installation:installation>` instructions.

   .. Note:: A service is a process that runs in the background. Running multiple instances of Sunshine is not
      advised.

   **Basic usage**
      .. code-block:: bash

         sunshine

   **Specify config file**
      .. code-block:: bash

         sunshine <directory of conf file>/sunshine.conf

      .. Note:: You do not need to specify a config file. If no config file is entered the default location will be used.

      .. Attention:: The configuration file specified will be created if it doesn't exist.

#. Configure Sunshine in the web ui

   The web ui is available on `https://localhost:47990 <https://localhost:47990>`_ by default. You may replace
   `localhost` with your internal ip address.

   .. Attention:: Ignore any warning given by your browser about "insecure website". This is due to the SSL certificate
      being self signed.

   .. Caution:: If running for the first time, make sure to note the username and password that you created.

   **Add games and applications.**
         This can be configured in the web ui.

         .. Note:: Additionally, apps can be configured manually. `src_assets/<os>/config/apps.json` is an example of a
            list of applications that are started just before running a stream. This is the directory within the GitHub
            repo.

#. In Moonlight, you may need to add the PC manually.
#. When Moonlight request you insert the correct pin on sunshine:

   - Login to the web ui
   - Go to "PIN" in the Navbar
   - Type in your PIN and press Enter, you should get a Success Message
   - In Moonlight, select one of the Applications listed

Network
-------
The Sunshine user interface will be available on port 47990 by default.

.. Warning:: Exposing ports to the internet can be dangerous. Do this at your own risk.

Arguments
---------
To get a list of available arguments run the following:
   .. code-block:: bash

      sunshine --help

Setup
-----

Linux
^^^^^
The `deb`, `rpm`, `Flatpak` and `AppImage` packages handle these steps automatically. Third party packages may not.

Sunshine needs access to `uinput` to create mouse and gamepad events.

#. Add user to group `input`, if this is the first time installing.
      .. code-block:: bash

         sudo usermod -a -G input $USER

#. Create `udev` rules.
      .. code-block::

         echo 'KERNEL=="uinput", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"' | \
         sudo tee /etc/udev/rules.d/85-sunshine-input.rules

#. Optionally, configure autostart service

   - filename: ``~/.config/systemd/user/sunshine.service``
   - contents:
         .. code-block::

            [Unit]
            Description=Sunshine self-hosted game stream host for Moonlight.
            StartLimitIntervalSec=500
            StartLimitBurst=5

            [Service]
            ExecStart=<see table>
            Restart=on-failure
            RestartSec=5s
            #Flatpak Only
            #ExecStop=flatpak kill dev.lizardbyte.sunshine

            [Install]
            WantedBy=graphical-session.target

         .. table::
            :widths: auto

            ========   ==============================================   ===============
            package    ExecStart                                        Auto Configured
            ========   ==============================================   ===============
            aur        /usr/bin/sunshine                                ✔
            deb        /usr/bin/sunshine                                ✔
            rpm        /usr/bin/sunshine                                ✔
            AppImage   ~/sunshine.AppImage                              ✔
            Flatpak    flatpak run dev.lizardbyte.sunshine              ✔
            ========   ==============================================   ===============

   **Start once**
         .. code-block:: bash

            systemctl --user start sunshine

   **Start on boot**
         .. code-block:: bash

            systemctl --user enable sunshine

#. Additional Setup for KMS
      .. Note:: ``cap_sys_admin`` may as well be root, except you don't need to be root to run it. It is necessary to
         allow Sunshine to use KMS.

      **Enable**
         .. code-block:: bash

            sudo setcap cap_sys_admin+p $(readlink -f $(which sunshine))

      **Disable (for Xorg/X11)**
         .. code-block:: bash

            sudo setcap -r $(readlink -f $(which sunshine))

#. Reboot
      .. code-block:: bash

         sudo reboot now

macOS
^^^^^
Sunshine can only access microphones on macOS due to system limitations. To stream system audio use
`Soundflower <https://github.com/mattingalls/Soundflower>`_ or
`BlackHole <https://github.com/ExistentialAudio/BlackHole>`_.

.. Note:: Command Keys are not forwarded by Moonlight. Right Option-Key is mapped to CMD-Key.

.. Caution:: Gamepads are not currently supported.

Configure autostart service
   **MacPorts**
      .. code-block:: bash

         sudo port load Sunshine

Windows
^^^^^^^
For gamepad support, install `ViGEmBus <https://github.com/ViGEm/ViGEmBus/releases/latest>`_

Sunshine firewall
   **Add rule**
      .. code-block:: batch

         cd /d "C:\Program Files\Sunshine\scripts"
         add-firewall-rule.bat

   **Remove rule**
      .. code-block:: batch

         cd /d "C:\Program Files\Sunshine\scripts"
         remove-firewall-rule.bat

Sunshine service
   **Enable**
      .. code-block:: batch

         cd /d "C:\Program Files\Sunshine\scripts"
         install-service.bat

   **Disable**
      .. code-block:: batch

         cd /d "C:\Program Files\Sunshine\scripts"
         uninstall-service.bat

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
- Example application:
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
   - ``prep-cmd`` - A list of commands to be run before/after the application

     - If any of the prep-commands fail, starting the application is aborted
     - ``do`` - Run before the application

       - If it fails, all ``undo`` commands of the previously succeeded ``do`` commands are run

     - ``undo`` - Run after the application has terminated

       - Failures of ``undo`` commands are ignored

   - ``working-dir`` - The working directory to use. If not specified, Sunshine will use the application directory.

Considerations
--------------
- When an application is started, if there is an application already running, it will be terminated.
- When the application has been shutdown, the stream shuts down as well.

  - For example, if you attempt to run ``steam`` as a ``cmd`` instead of ``detached`` the stream will immediately fail.
    This is due to the method in which the steam process is executed. Other applications may behave similarly.

- In addition to the apps listed, one app "Desktop" is hardcoded into Sunshine. It does not start an application,
  instead it simply starts a stream.
- For the Linux flatpak you must prepend commands with ``flatpak-spawn --host``.

HDR Support
-----------
Streaming HDR content is supported for Windows hosts with NVIDIA, AMD, or Intel GPUs that support encoding HEVC Main 10.
You must have an HDR-capable display or EDID emulator dongle connected to your host PC to activate HDR in Windows.

- Ensure you enable the HDR option in your Moonlight client settings, otherwise the stream will be SDR.
- A good HDR experience relies on proper HDR display calibration both in Windows and in game. HDR calibration can differ significantly between client and host displays.
- We recommend calibrating the display by streaming the Windows HDR Calibration app to your client device and saving an HDR calibration profile to use while streaming.
- You may also need to tune the brightness slider or HDR calibration options in game to the different HDR brightness capabilities of your client's display.
- Older games that use NVIDIA-specific NVAPI HDR rather than native Windows 10 OS HDR support may not display in HDR.
- Some GPUs can produce lower image quality or encoding performance when streaming in HDR compared to SDR.

Tutorials
---------
Tutorial videos are available `here <https://www.youtube.com/playlist?list=PLMYr5_xSeuXAbhxYHz86hA1eCDugoxXY0>`_.

.. admonition:: Community!

   Tutorials are community generated. Want to contribute? Reach out to us on our discord server.
