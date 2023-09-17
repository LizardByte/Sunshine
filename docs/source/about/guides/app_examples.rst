App Examples
============
Since not all applications behave the same, we decided to create some examples to help you get started adding games
and applications to Sunshine.

.. Attention:: Throughout these examples, any fields not shown are left blank. You can enhance your experience by
   adding an image or a log file (via the ``Output`` field).

Common Examples
---------------

Desktop
^^^^^^^

+----------------------+-----------------+
| **Field**            | **Value**       |
+----------------------+-----------------+
| Application Name     | ``Desktop``     |
+----------------------+-----------------+
| Image                | ``desktop.png`` |
+----------------------+-----------------+

Steam Big Picture
^^^^^^^^^^^^^^^^^

.. Note:: Steam is launched as a detached command because Steam starts with a process that self updates itself and the original
   process is killed. Since the original process ends it will not work as a regular command.

+----------------------+------------------------------------------+----------------------------------+-----------------------------------+
| **Field**            | **Linux**                                | **macOS**                        | **Windows**                       |
+----------------------+------------------------------------------+----------------------------------+-----------------------------------+
| Application Name     | ``Steam Big Picture``                                                                                           |
+----------------------+------------------------------------------+----------------------------------+-----------------------------------+
| Detached Commands    | ``setsid steam steam://open/bigpicture`` | ``open steam://open/bigpicture`` | ``steam steam://open/bigpicture`` |
+----------------------+------------------------------------------+----------------------------------+-----------------------------------+
| Image                | ``steam.png``                                                                                                   |
+----------------------+------------------------------------------+----------------------------------+-----------------------------------+

Epic Game Store game
^^^^^^^^^^^^^^^^^^^^

.. Note:: Using URI method will be the most consistent between various games, but does not allow a game to be launched
   using the "Command" and therefore the stream will not end when the game ends.

URI (Epic)
""""""""""

+----------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------+
| **Field**            | **Windows**                                                                                                                                               |
+----------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------+
| Application Name     | ``Surviving Mars``                                                                                                                                        |
+----------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------+
| Detached Commands    | ``cmd /C "start com.epicgames.launcher://apps/d759128018124dcabb1fbee9bb28e178%3A20729b9176c241f0b617c5723e70ec2d%3AOvenbird?action=launch&silent=true"`` |
+----------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------+

Binary (Epic w/ working directory)
""""""""""""""""""""""""""""""""""

+----------------------+-----------------------------------------------+
| **Field**            | **Windows**                                   |
+----------------------+-----------------------------------------------+
| Application Name     | ``Surviving Mars``                            |
+----------------------+-----------------------------------------------+
| Command              | ``cmd /c "MarsEpic.exe"``                     |
+----------------------+-----------------------------------------------+
| Working Directory    | ``C:\Program Files\Epic Games\SurvivingMars`` |
+----------------------+-----------------------------------------------+

Binary (Epic w/o working directory)
"""""""""""""""""""""""""""""""""""

+----------------------+--------------------------------------------------------------+
| **Field**            | **Windows**                                                  |
+----------------------+--------------------------------------------------------------+
| Application Name     | ``Surviving Mars``                                           |
+----------------------+--------------------------------------------------------------+
| Command              | ``"C:\Program Files\Epic Games\SurvivingMars\MarsEpic.exe"`` |
+----------------------+--------------------------------------------------------------+


Steam game
^^^^^^^^^^

.. Note:: Using URI method will be the most consistent between various games, but does not allow a game to be launched
   using the "Command" and therefore the stream will not end when the game ends.

URI (Steam)
"""""""""""

+----------------------+-------------------------------------------+-----------------------------------+---------------------------------------------+
| **Field**            | **Linux**                                 | **macOS**                         | **Windows**                                 |
+----------------------+-------------------------------------------+-----------------------------------+---------------------------------------------+
| Application Name     | ``Surviving Mars``                                                                                                          |
+----------------------+-------------------------------------------+-----------------------------------+---------------------------------------------+
| Detached Commands    | ``setsid steam steam://rungameid/464920`` | ``open steam://rungameid/464920`` | ``cmd /C "start steam://rungameid/464920"`` |
+----------------------+-------------------------------------------+-----------------------------------+---------------------------------------------+

Binary (Steam w/ working directory)
"""""""""""""""""""""""""""""""""""

+----------------------+-------------------------+-------------------------+------------------------------------------------------------------+
| **Field**            | **Linux**               | **macOS**               | **Windows**                                                      |
+----------------------+-------------------------+-------------------------+------------------------------------------------------------------+
| Application Name     | ``Surviving Mars``                                                                                                   |
+----------------------+-------------------------+-------------------------+------------------------------------------------------------------+
| Command              | ``MarsSteam``                                     | ``cmd /c "MarsSteam.exe"``                                       |
+----------------------+-------------------------+-------------------------+------------------------------------------------------------------+
| Working Directory    | ``~/.steam/steam/SteamApps/common/Survivng Mars`` | ``C:\Program Files (x86)\Steam\steamapps\common\Surviving Mars`` |
+----------------------+-------------------------+-------------------------+------------------------------------------------------------------+

Binary (Steam w/o working directory)
""""""""""""""""""""""""""""""""""""

+----------------------+------------------------------+------------------------------+----------------------------------------------------------------------------------+
| **Field**            | **Linux**                    | **macOS**                    | **Windows**                                                                      |
+----------------------+------------------------------+------------------------------+----------------------------------------------------------------------------------+
| Application Name     | ``Surviving Mars``                                                                                                                             |
+----------------------+------------------------------+------------------------------+----------------------------------------------------------------------------------+
| Command              | ``~/.steam/steam/SteamApps/common/Survivng Mars/MarsSteam`` | ``"C:\Program Files (x86)\Steam\steamapps\common\Surviving Mars\MarsSteam.exe"`` |
+----------------------+------------------------------+------------------------------+----------------------------------------------------------------------------------+

Linux
-----

Changing Resolution and Refresh Rate (Linux - X11)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

+----------------------+---------------------------------------------------------------------------------------------------------------------------------------+
| **Field**            | **Value**                                                                                                                             |
+----------------------+---------------------------------------------------------------------------------------------------------------------------------------+
| Command Preparations | Do: ``sh -c "xrandr --output HDMI-1 --mode \"${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}\" --rate ${SUNSHINE_CLIENT_FPS}"``    |
|                      +---------------------------------------------------------------------------------------------------------------------------------------+
|                      | Undo: ``xrandr --output HDMI-1 --mode 3840x2160 --rate 120``                                                                          |
+----------------------+---------------------------------------------------------------------------------------------------------------------------------------+

.. hint::
   The above only works if the xrandr mode already exists. You will need to create new modes to stream to macOS and iOS devices, since they use non standard resolutions.

   You can update the ``Do`` command to this:
      .. code-block:: bash

         bash -c "${HOME}/scripts/set-custom-res.sh \"${SUNSHINE_CLIENT_WIDTH}\" \"${SUNSHINE_CLIENT_HEIGHT}\" \"${SUNSHINE_CLIENT_FPS}\""

   The ``set-custom-res.sh`` will have this content:
      .. code-block:: bash

         #!/bin/bash

         # Get params and set any defaults
         width=${1:-1920}
         height=${2:-1080}
         refresh_rate=${3:-60}

         # You may need to adjust the scaling differently so the UI/text isn't too small / big
         scale=${4:-0.55}

         # Get the name of the active display
         display_output=$(xrandr | grep " connected" | awk '{ print $1 }')

         # Get the modeline info from the 2nd row in the cvt output
         modeline=$(cvt ${width} ${height} ${refresh_rate} | awk 'FNR == 2')
         xrandr_mode_str=${modeline//Modeline \"*\" /}
         mode_alias="${width}x${height}"

         echo "xrandr setting new mode ${mode_alias} ${xrandr_mode_str}"
         xrandr --newmode ${mode_alias} ${xrandr_mode_str}
         xrandr --addmode ${display_output} ${mode_alias}

         # Reset scaling
         xrandr --output ${display_output} --scale 1

         # Apply new xrandr mode
         xrandr --output ${display_output} --primary --mode ${mode_alias} --pos 0x0 --rotate normal --scale ${scale}

         # Optional reset your wallpaper to fit to new resolution
         # xwallpaper --zoom /path/to/wallpaper.png

Changing Resolution and Refresh Rate (Linux - Wayland)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

+----------------------+-------------------------------------------------------------------------------------------------------------------------------------+
| **Field**            | **Value**                                                                                                                           |
+----------------------+-------------------------------------------------------------------------------------------------------------------------------------+
| Command Preparations | Do: ``sh -c "wlr-xrandr --output HDMI-1 --mode \"${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}@${SUNSHINE_CLIENT_FPS}Hz\""``   |
|                      +-------------------------------------------------------------------------------------------------------------------------------------+
|                      | Undo: ``wlr-xrandr --output HDMI-1 --mode 3840x2160@120Hz``                                                                         |
+----------------------+-------------------------------------------------------------------------------------------------------------------------------------+

Changing Resolution and Refresh Rate (Linux - KDE Plasma - Wayland and X11)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

+----------------------+----------------------------------------------------------------------------------------------------------------------------------+
| **Field**            | **Value**                                                                                                                        |
+----------------------+----------------------------------------------------------------------------------------------------------------------------------+
| Command Preparations | Do: ``sh -c "kscreen-doctor output.HDMI-A-1.mode.${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}@${SUNSHINE_CLIENT_FPS}"``    |
|                      +----------------------------------------------------------------------------------------------------------------------------------+
|                      | Undo: ``kscreen-doctor output.HDMI-A-1.mode.3840x2160@120``                                                                      |
+----------------------+----------------------------------------------------------------------------------------------------------------------------------+

Flatpak
^^^^^^^

.. Attention:: Because Flatpak packages run in a sandboxed environment and do not normally have access to the host,
   the Flatpak of Sunshine requires commands to be prefixed with ``flatpak-spawn --host``.

macOS
-----

Changing Resolution and Refresh Rate (macOS)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. Note:: This example uses the `displayplacer` tool to change the resolution.
   This tool can be installed following instructions in their
   `GitHub repository <https://github.com/jakehilborn/displayplacer>`__.

+----------------------+-----------------------------------------------------------------------------------------------+
| **Field**            | **Value**                                                                                     |
+----------------------+-----------------------------------------------------------------------------------------------+
| Command Preparations | Do: ``displayplacer "id:<screenId> res:1920x1080 hz:60 scaling:on origin:(0,0) degree:0"``    |
|                      +-----------------------------------------------------------------------------------------------+
|                      | Undo: ``displayplacer "id:<screenId> res:3840x2160 hz:120 scaling:on origin:(0,0) degree:0"`` |
+----------------------+-----------------------------------------------------------------------------------------------+

Windows
-------

Changing Resolution and Refresh Rate (Windows)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. Note:: This example uses the `QRes` tool to change the resolution and refresh rate.
   This tool can be downloaded from their `SourceForge repository <https://sourceforge.net/projects/qres/>`__.

+----------------------+------------------------------------------------------------------------------------------------------------------+
| **Field**            | **Value**                                                                                                        |
+----------------------+------------------------------------------------------------------------------------------------------------------+
| Command Preparations | Do: ``cmd /C FullPath\qres.exe /x:%SUNSHINE_CLIENT_WIDTH% /y:%SUNSHINE_CLIENT_HEIGHT% /r:%SUNSHINE_CLIENT_FPS%`` |
|                      +------------------------------------------------------------------------------------------------------------------+
|                      | Undo: ``cmd /C FullPath\qres.exe /x:3840 /y:2160 /r:120``                                                        |
+----------------------+------------------------------------------------------------------------------------------------------------------+

Elevating Commands (Windows)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If you've installed Sunshine as a service (default), you can now specify if a command should be elevated with adminsitrative privileges.
Simply enable the elevated option in the WEB UI, or add it to the JSON configuration.
This is an option for both prep-cmd and regular commands and will launch the process with the current user without a UAC prompt.

.. Note:: It's important to write the values "true" and "false" as string values, not as the typical true/false values in most JSON.

**Example**
   .. code-block:: json

        {
            "name": "Game With AntiCheat that Requires Admin",
            "output": "",
            "cmd": "ping 127.0.0.1",
            "exclude-global-prep-cmd": "false",
            "elevated": "true",
            "prep-cmd": [
                {
                    "do": "powershell.exe -command \"Start-Streaming\"",
                    "undo": "powershell.exe -command \"Stop-Streaming\"",
                    "elevated": "false"
                }
            ],
            "image-path": ""
        }
