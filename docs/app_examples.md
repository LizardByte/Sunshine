# App Examples
Since not all applications behave the same, we decided to create some examples to help you get started adding games
and applications to Sunshine.

@attention{Throughout these examples, any fields not shown are left blank. You can enhance your experience by
adding an image or a log file (via the `Output` field).}

@note{When a working directory is not specified, it defaults to the folder where the target application resides.}


## Common Examples

### Desktop

| Field            | Value                      |
|------------------|----------------------------|
| Application Name | @code{}Desktop@endcode     |
| Image            | @code{}desktop.png@endcode |

### Steam Big Picture
@note{Steam is launched as a detached command because Steam starts with a process that self updates itself and the original
process is killed.}

@tabs{
  @tab{Linux | <!-- -->
    \| Field             \| Value                                               \|
    \|-------------------\|-----------------------------------------------------\|
    \| Application Name  \| @code{}Steam Big Picture@endcode                    \|
    \| Detached Commands \| @code{}setsid steam steam://open/bigpicture@endcode \|
    \| Image             \| @code{}steam.png@endcode                            \|
  }
  @tab{macOS | <!-- -->
    \| Field             \| Value                                             \|
    \|-------------------\|---------------------------------------------------\|
    \| Application Name  \| @code{}Steam Big Picture@endcode                  \|
    \| Detached Commands \| @code{}open steam steam://open/bigpicture@endcode \|
    \| Image             \| @code{}steam.png@endcode                          \|
  }
  @tab{Windows | <!-- -->
    \| Field             \| Value                                  \|
    \|-------------------\|----------------------------------------\|
    \| Application Name  \| @code{}Steam Big Picture@endcode       \|
    \| Detached Commands \| @code{}steam://open/bigpicture@endcode \|
    \| Image             \| @code{}steam.png@endcode               \|
  }
}

### Epic Game Store game
@note{Using URI method will be the most consistent between various games.}

#### URI

@tabs{
  @tab{Windows | <!-- -->
    \| Field            \| Value                                                                                                                                                 \|
    \|------------------\|-------------------------------------------------------------------------------------------------------------------------------------------------------\|
    \| Application Name \| @code{}Surviving Mars@endcode                                                                                                                         \|
    \| Commands         \| @code{}com.epicgames.launcher://apps/d759128018124dcabb1fbee9bb28e178%3A20729b9176c241f0b617c5723e70ec2d%3AOvenbird?action=launch&silent=true@endcode \|
  }
}

#### Binary (w/ working directory
@tabs{
  @tab{Windows | <!-- -->
    \| Field             \| Value                                                      \|
    \|-------------------\|------------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                              \|
    \| Command           \| @code{}MarsEpic.exe@endcode                                \|
    \| Working Directory \| @code{}"C:\Program Files\Epic Games\SurvivingMars"@endcode \|
  }
}

#### Binary (w/o working directory)
@tabs{
  @tab{Windows | <!-- -->
    \| Field             \| Value                                                                   \|
    \|-------------------\|-------------------------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                                           \|
    \| Command           \| @code{}"C:\Program Files\Epic Games\SurvivingMars\MarsEpic.exe"@endcode \|
  }
}

### Steam game
@note{Using URI method will be the most consistent between various games.}

#### URI

@tabs{
  @tab{Linux | <!-- -->
    \| Field             \| Value                                                \|
    \|-------------------\|------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                        \|
    \| Detached Commands \| @code{}setsid steam steam://rungameid/464920@endcode \|
  }
  @tab{macOS | <!-- -->
    \| Field             \| Value                                        \|
    \|-------------------\|----------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                \|
    \| Detached Commands \| @code{}open steam://rungameid/464920@endcode \|
  }
  @tab{Windows | <!-- -->
    \| Field             \| Value                                   \|
    \|-------------------\|-----------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode           \|
    \| Detached Commands \| @code{}steam://rungameid/464920@endcode \|
  }
}

#### Binary (w/ working directory
@tabs{
  @tab{Linux | <!-- -->
    \| Field             \| Value                                                        \|
    \|-------------------\|--------------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                                \|
    \| Command           \| @code{}MarsSteam@endcode                                     \|
    \| Working Directory \| @code{}~/.steam/steam/SteamApps/common/Survivng Mars@endcode \|
  }
  @tab{macOS | <!-- -->
    \| Field             \| Value                                                        \|
    \|-------------------\|--------------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                                \|
    \| Command           \| @code{}MarsSteam@endcode                                     \|
    \| Working Directory \| @code{}~/.steam/steam/SteamApps/common/Survivng Mars@endcode \|
  }
  @tab{Windows | <!-- -->
    \| Field             \| Value                                                                         \|
    \|-------------------\|-------------------------------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                                                 \|
    \| Command           \| @code{}MarsSteam.exe@endcode                                                  \|
    \| Working Directory \| @code{}"C:\Program Files (x86)\Steam\steamapps\common\Surviving Mars"@endcode \|
  }
}

#### Binary (w/o working directory)
@tabs{
  @tab{Linux | <!-- -->
    \| Field             \| Value                                                                  \|
    \|-------------------\|------------------------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                                          \|
    \| Command           \| @code{}~/.steam/steam/SteamApps/common/Survivng Mars/MarsSteam@endcode \|
  }
  @tab{macOS | <!-- -->
    \| Field             \| Value                                                                  \|
    \|-------------------\|------------------------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                                          \|
    \| Command           \| @code{}~/.steam/steam/SteamApps/common/Survivng Mars/MarsSteam@endcode \|
  }
  @tab{Windows | <!-- -->
    \| Field             \| Value                                                                                       \|
    \|-------------------\|---------------------------------------------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                                                               \|
    \| Command           \| @code{}"C:\Program Files (x86)\Steam\steamapps\common\Surviving Mars\MarsSteam.exe"@endcode \|
  }
}

### Prep Commands

#### Changing Resolution and Refresh Rate

##### Linux

###### X11

| Prep Step | Command                                                                                                                               |
|-----------|---------------------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}sh -c "xrandr --output HDMI-1 --mode ${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT} --rate ${SUNSHINE_CLIENT_FPS}"@endcode |
| Undo      | @code{}xrandr --output HDMI-1 --mode 3840x2160 --rate 120@endcode                                                                     |

@hint{The above only works if the xrandr mode already exists. You will need to create new modes to stream to macOS
and iOS devices, since they use non standard resolutions.

You can update the ``Do`` command to this:
```bash
bash -c "${HOME}/scripts/set-custom-res.sh \"${SUNSHINE_CLIENT_WIDTH}\" \"${SUNSHINE_CLIENT_HEIGHT}\" \"${SUNSHINE_CLIENT_FPS}\""
```

The `set-custom-res.sh` will have this content:
```bash
#!/bin/bash
set -e

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
```
}

###### Wayland

| Prep Step | Command                                                                                                                                  |
|-----------|------------------------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}sh -c "wlr-xrandr --output HDMI-1 --mode \"${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}@${SUNSHINE_CLIENT_FPS}Hz\""@endcode |
| Undo      | @code{}wlr-xrandr --output HDMI-1 --mode 3840x2160@120Hz@endcode                                                                         |

@hint{`wlr-xrandr` only works with wlroots-based compositors.}

###### Gnome (Wayland, X11)

| Prep Step | Command                                                                                                                               |
|-----------|---------------------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}sh -c "xrandr --output HDMI-1 --mode ${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT} --rate ${SUNSHINE_CLIENT_FPS}"@endcode |
| Undo      | @code{}xrandr --output HDMI-1 --mode 3840x2160 --rate 120@endcode                                                                     |

The commands above are valid for an X11 session but won't work for
Wayland. In that case `xrandr` must be replaced by [gnome-randr.py](https://gitlab.com/Oschowa/gnome-randr).
This script is intended as a drop-in replacement with the same syntax. (It can be saved in
`/usr/local/bin` and needs to be made executable.)

###### KDE Plasma (Wayland, X11)

| Prep Step | Command                                                                                                                              |
|-----------|--------------------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}sh -c "kscreen-doctor output.HDMI-A-1.mode.${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}@${SUNSHINE_CLIENT_FPS}"@endcode |
| Undo      | @code{}kscreen-doctor output.HDMI-A-1.mode.3840x2160@120@endcode                                                                     |

@attention{The names of your displays will differ between X11 and Wayland.
Be sure to use the correct name, depending on your session manager.
e.g. On X11, the monitor may be called ``HDMI-A-0``, but on Wayland, it may be called ``HDMI-A-1``.
}

@hint{Replace ``HDMI-A-1`` with the display name of the monitor you would like to use for Moonlight.
You can list the monitors available to you with:
```
kscreen-doctor -o
```

These will also give you the supported display properties for each monitor. You can select them either by
hard-coding their corresponding number (e.g. ``kscreen-doctor output.HDMI-A1.mode.0``) or using the above
``do`` command to fetch the resolution requested by your Moonlight client
(which has a chance of not being supported by your monitor).
}

###### NVIDIA

| Prep Step | Command                                                                                                     |
|-----------|-------------------------------------------------------------------------------------------------------------|
| Do        | @code{}sh -c "${HOME}/scripts/set-custom-res.sh ${SUNSHINE_CLIENT_WIDTH} ${SUNSHINE_CLIENT_HEIGHT}"@endcode |
| Undo      | @code{}sh -c "${HOME}/scripts/set-custom-res.sh 3840 2160"@endcode                                          |

The ``set-custom-res.sh`` will have this content:
```bash
#!/bin/bash
set -e

# Get params and set any defaults
width=${1:-1920}
height=${2:-1080}
output=${3:-HDMI-1}
nvidia-settings -a CurrentMetaMode="${output}: nvidia-auto-select { ViewPortIn=${width}x${height}, ViewPortOut=${width}x${height}+0+0 }"
```

##### macOS

###### displayplacer
@note{This example uses the `displayplacer` tool to change the resolution.
This tool can be installed following instructions in their
[GitHub repository](https://github.com/jakehilborn/displayplacer)}.

| Prep Step | Command                                                                                            |
|-----------|----------------------------------------------------------------------------------------------------|
| Do        | @code{}displayplacer "id:<screenId> res:1920x1080 hz:60 scaling:on origin:(0,0) degree:0"@endcode  |
| Undo      | @code{}displayplacer "id:<screenId> res:3840x2160 hz:120 scaling:on origin:(0,0) degree:0"@endcode |

##### Windows

###### QRes
@note{This example uses the *QRes* tool to change the resolution and refresh rate.
This tool can be downloaded from their [SourceForge repository](https://sourceforge.net/projects/qres).}.

| Prep Step | Command                                                                                                                 |
|-----------|-------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}cmd /C FullPath\qres.exe /x:%SUNSHINE_CLIENT_WIDTH% /y:%SUNSHINE_CLIENT_HEIGHT% /r:%SUNSHINE_CLIENT_FPS%@endcode |
| Undo      | @code{}cmd /C FullPath\qres.exe /x:3840 /y:2160 /r:120@endcode                                                          |

### Additional Considerations

#### Linux (Flatpak)
@attention{Because Flatpak packages run in a sandboxed environment and do not normally have access to the
host, the Flatpak of Sunshine requires commands to be prefixed with `flatpak-spawn --host`.}

#### Windows
**Elevating Commands (Windows)**

If you've installed Sunshine as a service (default), you can specify if a command should be elevated with
administrative privileges. Simply enable the elevated option in the WEB UI, or add it to the JSON configuration.
This is an option for both prep-cmd and regular commands and will launch the process with the current user without a
UAC prompt.

@note{It is important to write the values "true" and "false" as string values, not as the typical true/false
values in most JSON.}

**Example**
```json
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
```

<div class="section_buttons">

| Previous                          |                Next |
|:----------------------------------|--------------------:|
| [Configuration](configuration.md) | [Guides](guides.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
