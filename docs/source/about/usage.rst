:github_url: https://github.com/SunshineStream/Sunshine/tree/nightly/docs/source/about/usage.rst

Usage
=====
#. See the `setup`_ section for your specific OS.
#. Run ``sunshine <directory of conf file>/sunshine.conf``.

   .. Note:: You do not need to specify a config file. If no config file is entered the default location will be used.

   .. Attention:: The configuration file specified will be created if it doesn't exist.

   .. Tip:: If using the Linux AppImage, replace ``sunshine`` with ``./sunshine.AppImage``

#. Configure Sunshine in the web ui
   The web ui is available on `https://localhost:47990 <https://localhost:47990>`_ by default. You may replace
   `localhost` with your internal ip address.

   .. Attention:: Ignore any warning given by your browser about "insecure website".

   .. Caution:: If running for the first time, make sure to note the username and password Sunshine showed to you,
      since you cannot get back later!

   Add games and applications.
      This can be configured in the web ui.

      .. Note:: Additionally, apps can be configured manually. `assets/apps_<os>.json` is an example of a list of
         applications that are started just before running a stream.

      .. Attention:: Application list is not fully supported on MacOS

#. In Moonlight, you may need to add the PC manually.
#. When Moonlight request you insert the correct pin on sunshine:

   - Login to the web ui
   - Go to "PIN" in the Header
   - Type in your PIN and press Enter, you should get a Success Message
   - In Moonlight, select one of the Applications listed

Network
-------
Sunshine will be available on port 47990 by default.

.. Danger:: Do not expose port 47990, or the web ui, to the internet!

Arguments
---------
To get a list of available arguments run the following:

   .. code-block:: bash

      sunshine --help

Setup
-----

Linux
^^^^^
Sunshine needs access to `uinput` to create mouse and gamepad events.

Add user to group `input`.
   .. code-block:: bash

      usermod -a -G input $USER

Create `udev` rules.
   .. code-block:: bash

      nano /etc/udev/rules.d/85-sunshine-input.rules

   Input the following contents:
   ``KERNEL=="uinput", GROUP="input", MODE="0660"``

   Save the file and exit:

      #. ``CTRL+X`` to start exit.
      #. ``Y`` to save modifications.

Configure autostart service
   `path/to/build/dir/sunshine.service` is used to start sunshine in the background. To use it, do the following:

   #. Copy it to the users systemd: ``cp sunshine.service ~/.config/systemd/user/``
   #. Starting

      - One time: ``systemctl --user start sunshine``
      - Always on boot: ``systemctl --user enable sunshine``

Additional Setup for KMS
   .. Note:: ``cap_sys_admin`` may as well be root, except you don't need to be root to run it.

   It is necessary to allow Sunshine to use KMS: ``sudo setcap cap_sys_admin+p sunshine``

MacOS
^^^^^
Sunshine can only access microphones on macOS due to system limitations. To stream system audio use
`Soundflower <https://github.com/mattingalls/Soundflower>`_ or
`BlackHole <https://github.com/ExistentialAudio/BlackHole>`_ and
select their sink as audio device in `sunshine.conf`.

.. Note:: Command Keys are not forwarded by Moonlight. Right Option-Key is mapped to CMD-Key.

.. Caution:: Gamepads are not supported.

Windows
^^^^^^^
For gamepad support, install `ViGEmBus <https://github.com/ViGEm/ViGEmBus/releases/latest>`_

Shortcuts
---------
All shortcuts start with CTRL + ALT + SHIFT, just like Moonlight

   - ``CTRL + ALT + SHIFT + N`` - Hide/Unhide the cursor (This may be useful for Remote Desktop Mode for Moonlight)
   - ``CTRL + ALT + SHIFT + F1/F13`` - Switch to different monitor for Streaming

Application List
----------------
- You can use Environment variables in place of values
- ``$(HOME)` will be replaced by the value of ``$HOME``
- ``$$`` will be replaced by ``$``, e.g. ``$$(HOME)`` will be replaced by ``$(HOME)``
- ``env`` - Adds or overwrites Environment variables for the commands/applications run by Sunshine
- ``"Variable name":"Variable value"``
- ``apps`` - The list of applications
- Example application:

   .. code-block:: json

      {
      "name":"An App",
      "cmd":"command to open app",
      "prep-cmd":[
      		{
      			"do":"some-command",
      			"undo":"undo-that-command"
      		}
      	],
      "detached":[
      	"some-command",
      	"another-command"
      	]
      }

   - ``name`` - The name of the application/game
   - ``output`` - The file where the output of the command is stored
   - ``detached`` - A list of commands to be run and forgotten about
   - ``prep-cmd`` - A list of commands to be run before/after the application

      - If any of the prep-commands fail, starting the application is aborted
      - ``do`` - Run before the application

         - If it fails, all ``undo`` commands of the previously succeeded ``do`` commands are run

      - ``undo`` - Run after the application has terminated

         - This should not fail considering it is supposed to undo the ``do`` commands
         - If it fails, Sunshine is terminated

      - ``cmd`` - The main application

         - If not specified, a process is started that sleeps indefinitely

Considerations
--------------
- When an application is started, if there is an application already running, it will be terminated.
- When the application has been shutdown, the stream shuts down as well.
- In addition to the apps listed, one app "Desktop" is hardcoded into Sunshine. It does not start an application,
  instead it simply starts a stream.
