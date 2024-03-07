App Examples
============
Since not all applications behave the same, we decided to create some examples to help you get started adding games
and applications to Sunshine.

.. attention:: Throughout these examples, any fields not shown are left blank. You can enhance your experience by
   adding an image or a log file (via the ``Output`` field).

.. note:: When a working directory is not specified, it defaults to the folder where the target application resides.

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

.. note:: Steam is launched as a detached command because Steam starts with a process that self updates itself and the original
   process is killed.

.. tab:: Linux

   +----------------------+------------------------------------------+
   | Application Name     | ``Steam Big Picture``                    |
   +----------------------+------------------------------------------+
   | Detached Commands    | ``setsid steam steam://open/bigpicture`` |
   +----------------------+------------------------------------------+
   | Image                | ``steam.png``                            |
   +----------------------+------------------------------------------+

.. tab:: macOS

   +----------------------+----------------------------------+
   | Application Name     | ``Steam Big Picture``            |
   +----------------------+----------------------------------+
   | Detached Commands    | ``open steam://open/bigpicture`` |
   +----------------------+----------------------------------+
   | Image                | ``steam.png``                    |
   +----------------------+----------------------------------+

.. tab:: Windows

   +----------------------+-----------------------------+
   | Application Name     | ``Steam Big Picture``       |
   +----------------------+-----------------------------+
   | Command              | ``steam://open/bigpicture`` |
   +----------------------+-----------------------------+
   | Image                | ``steam.png``               |
   +----------------------+-----------------------------+

Epic Game Store game
^^^^^^^^^^^^^^^^^^^^

.. note:: Using URI method will be the most consistent between various games.

URI (Epic)
""""""""""

.. tab:: Windows

   +----------------------+--------------------------------------------------------------------------------------------------------------------------------------------+
   | Application Name     | ``Surviving Mars``                                                                                                                         |
   +----------------------+--------------------------------------------------------------------------------------------------------------------------------------------+
   | Command              | ``com.epicgames.launcher://apps/d759128018124dcabb1fbee9bb28e178%3A20729b9176c241f0b617c5723e70ec2d%3AOvenbird?action=launch&silent=true`` |
   +----------------------+--------------------------------------------------------------------------------------------------------------------------------------------+

Binary (Epic w/ working directory)
""""""""""""""""""""""""""""""""""

.. tab:: Windows

   +----------------------+-----------------------------------------------+
   | Application Name     | ``Surviving Mars``                            |
   +----------------------+-----------------------------------------------+
   | Command              | ``MarsEpic.exe``                              |
   +----------------------+-----------------------------------------------+
   | Working Directory    | ``C:\Program Files\Epic Games\SurvivingMars`` |
   +----------------------+-----------------------------------------------+

Binary (Epic w/o working directory)
"""""""""""""""""""""""""""""""""""

.. tab:: Windows

   +----------------------+--------------------------------------------------------------+
   | Application Name     | ``Surviving Mars``                                           |
   +----------------------+--------------------------------------------------------------+
   | Command              | ``"C:\Program Files\Epic Games\SurvivingMars\MarsEpic.exe"`` |
   +----------------------+--------------------------------------------------------------+

Steam game
^^^^^^^^^^

.. note:: Using URI method will be the most consistent between various games.

URI (Steam)
"""""""""""

.. tab:: Linux

   +----------------------+-------------------------------------------+
   | Application Name     | ``Surviving Mars``                        |
   +----------------------+-------------------------------------------+
   | Detached Commands    | ``setsid steam steam://rungameid/464920`` |
   +----------------------+-------------------------------------------+

.. tab:: macOS

   +----------------------+-----------------------------------+
   | Application Name     | ``Surviving Mars``                |
   +----------------------+-----------------------------------+
   | Detached Commands    | ``open steam://rungameid/464920`` |
   +----------------------+-----------------------------------+

.. tab:: Windows

   +----------------------+------------------------------+
   | Application Name     | ``Surviving Mars``           |
   +----------------------+------------------------------+
   | Command              | ``steam://rungameid/464920`` |
   +----------------------+------------------------------+

Binary (Steam w/ working directory)
"""""""""""""""""""""""""""""""""""

.. tab:: Linux

   +----------------------+---------------------------------------------------+
   | Application Name     | ``Surviving Mars``                                |
   +----------------------+---------------------------------------------------+
   | Command              | ``MarsSteam``                                     |
   +----------------------+---------------------------------------------------+
   | Working Directory    | ``~/.steam/steam/SteamApps/common/Survivng Mars`` |
   +----------------------+---------------------------------------------------+

.. tab:: macOS

   +----------------------+---------------------------------------------------+
   | Application Name     | ``Surviving Mars``                                |
   +----------------------+---------------------------------------------------+
   | Command              | ``MarsSteam``                                     |
   +----------------------+---------------------------------------------------+
   | Working Directory    | ``~/.steam/steam/SteamApps/common/Survivng Mars`` |
   +----------------------+---------------------------------------------------+

.. tab:: Windows

   +----------------------+------------------------------------------------------------------+
   | Application Name     | ``Surviving Mars``                                               |
   +----------------------+------------------------------------------------------------------+
   | Command              | ``MarsSteam.exe``                                                |
   +----------------------+------------------------------------------------------------------+
   | Working Directory    | ``C:\Program Files (x86)\Steam\steamapps\common\Surviving Mars`` |
   +----------------------+------------------------------------------------------------------+

Binary (Steam w/o working directory)
""""""""""""""""""""""""""""""""""""

.. tab:: Linux

   +----------------------+-------------------------------------------------------------+
   | Application Name     | ``Surviving Mars``                                          |
   +----------------------+-------------------------------------------------------------+
   | Command              | ``~/.steam/steam/SteamApps/common/Survivng Mars/MarsSteam`` |
   +----------------------+-------------------------------------------------------------+

.. tab:: macOS

   +----------------------+-------------------------------------------------------------+
   | Application Name     | ``Surviving Mars``                                          |
   +----------------------+-------------------------------------------------------------+
   | Command              | ``~/.steam/steam/SteamApps/common/Survivng Mars/MarsSteam`` |
   +----------------------+-------------------------------------------------------------+

.. tab:: Windows

   +----------------------+----------------------------------------------------------------------------------+
   | Application Name     | ``Surviving Mars``                                                               |
   +----------------------+----------------------------------------------------------------------------------+
   | Command              | ``"C:\Program Files (x86)\Steam\steamapps\common\Surviving Mars\MarsSteam.exe"`` |
   +----------------------+----------------------------------------------------------------------------------+

Prep Commands
-------------

Changing Resolution and Refresh Rate
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. tab:: Linux

   .. tab:: X11

      +----------------------+------------------------------------------------------------------------------------------------------------------------------------+
      | Command Preparations | Do: ``sh -c "xrandr --output HDMI-1 --mode \"${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}\" --rate ${SUNSHINE_CLIENT_FPS}"`` |
      |                      +------------------------------------------------------------------------------------------------------------------------------------+
      |                      | Undo: ``xrandr --output HDMI-1 --mode 3840x2160 --rate 120``                                                                       |
      +----------------------+------------------------------------------------------------------------------------------------------------------------------------+

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

   .. tab:: Wayland

      +----------------------+-----------------------------------------------------------------------------------------------------------------------------------+
      | Command Preparations | Do: ``sh -c "wlr-xrandr --output HDMI-1 --mode \"${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}@${SUNSHINE_CLIENT_FPS}Hz\""`` |
      |                      +-----------------------------------------------------------------------------------------------------------------------------------+
      |                      | Undo: ``wlr-xrandr --output HDMI-1 --mode 3840x2160@120Hz``                                                                       |
      +----------------------+-----------------------------------------------------------------------------------------------------------------------------------+

   .. tab:: KDE Plasma (Wayland, X11)

      +----------------------+-------------------------------------------------------------------------------------------------------------------------------+
      | Command Preparations | Do: ``sh -c "kscreen-doctor output.HDMI-A-1.mode.${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}@${SUNSHINE_CLIENT_FPS}"`` |
      |                      +-------------------------------------------------------------------------------------------------------------------------------+
      |                      | Undo: ``kscreen-doctor output.HDMI-A-1.mode.3840x2160@120``                                                                   |
      +----------------------+-------------------------------------------------------------------------------------------------------------------------------+

   .. tab:: NVIDIA

      +----------------------+------------------------------------------------------------------------------------------------------+
      | Command Preparations | Do: ``sh -c "${HOME}/scripts/set-custom-res.sh ${SUNSHINE_CLIENT_WIDTH} ${SUNSHINE_CLIENT_HEIGHT}"`` |
      |                      +------------------------------------------------------------------------------------------------------+
      |                      | Undo: ``sh -c "${HOME}/scripts/set-custom-res.sh 3840 2160"``                                        |
      +----------------------+------------------------------------------------------------------------------------------------------+

      The ``set-custom-res.sh`` will have this content:
         .. code-block:: bash

            #!/bin/bash

            # Get params and set any defaults
            width=${1:-1920}
            height=${2:-1080}
            output=${3:-HDMI-1}
            nvidia-settings -a CurrentMetaMode="${output}: nvidia-auto-select { ViewPortIn=${width}x${height}, ViewPortOut=${width}x${height}+0+0 }"

.. tab:: macOS

   .. tab:: displayplacer

      .. note:: This example uses the `displayplacer` tool to change the resolution.
         This tool can be installed following instructions in their
         `GitHub repository <https://github.com/jakehilborn/displayplacer>`__.

      +----------------------+-----------------------------------------------------------------------------------------------+
      | Command Preparations | Do: ``displayplacer "id:<screenId> res:1920x1080 hz:60 scaling:on origin:(0,0) degree:0"``    |
      |                      +-----------------------------------------------------------------------------------------------+
      |                      | Undo: ``displayplacer "id:<screenId> res:3840x2160 hz:120 scaling:on origin:(0,0) degree:0"`` |
      +----------------------+-----------------------------------------------------------------------------------------------+

.. tab:: Windows

   .. tab:: QRes

      .. note:: This example uses the `QRes` tool to change the resolution and refresh rate.
         This tool can be downloaded from their `SourceForge repository <https://sourceforge.net/projects/qres/>`__.

      +----------------------+------------------------------------------------------------------------------------------------------------------+
      | Command Preparations | Do: ``cmd /C FullPath\qres.exe /x:%SUNSHINE_CLIENT_WIDTH% /y:%SUNSHINE_CLIENT_HEIGHT% /r:%SUNSHINE_CLIENT_FPS%`` |
      |                      +------------------------------------------------------------------------------------------------------------------+
      |                      | Undo: ``cmd /C FullPath\qres.exe /x:3840 /y:2160 /r:120``                                                        |
      +----------------------+------------------------------------------------------------------------------------------------------------------+

Additional Considerations
-------------------------

.. tab:: Linux

   .. tab:: Flatpak

      .. attention:: Because Flatpak packages run in a sandboxed environment and do not normally have access to the
         host, the Flatpak of Sunshine requires commands to be prefixed with ``flatpak-spawn --host``.

.. tab:: Windows

   **Elevating Commands (Windows)**

   If you've installed Sunshine as a service (default), you can specify if a command should be elevated with
   administrative privileges. Simply enable the elevated option in the WEB UI, or add it to the JSON configuration.
   This is an option for both prep-cmd and regular commands and will launch the process with the current user without a
   UAC prompt.

   .. note:: It is important to write the values "true" and "false" as string values, not as the typical true/false
      values in most JSON.

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
