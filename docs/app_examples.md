# App Examples
Since not all applications behave the same, we decided to create some examples to help you get started adding games
and applications to Sunshine.

> [!TIP]
> Throughout these examples, any fields not shown are left blank. You can enhance your experience by
> adding an image or a log file (via the `Output` field).

> [!WARNING]
> When a working directory is not specified, it defaults to the folder where the target application resides.


## Common Examples

### Desktop

| Field            | Value                      |
|------------------|----------------------------|
| Application Name | @code{}Desktop@endcode     |
| Image            | @code{}desktop.png@endcode |

### Steam Big Picture

> [!NOTE]
> Steam is launched as a detached command because Steam starts with a process that self updates itself and the original
> process is killed.

@tabs{
  @tab{FreeBSD | <!-- -->
    \| Field                        \| Value                                                \|
    \|------------------------------\|------------------------------------------------------\|
    \| Application Name             \| @code{}Steam Big Picture@endcode                     \|
    \| Command Preporations -> Undo \| @code{}setsid steam steam://close/bigpicture@endcode \|
    \| Detached Commands            \| @code{}setsid steam steam://open/bigpicture@endcode  \|
    \| Image                        \| @code{}steam.png@endcode                             \|
  }
  @tab{Linux | <!-- -->
    \| Field                        \| Value                                                \|
    \|------------------------------\|------------------------------------------------------\|
    \| Application Name             \| @code{}Steam Big Picture@endcode                     \|
    \| Command Preporations -> Undo \| @code{}setsid steam steam://close/bigpicture@endcode \|
    \| Detached Commands            \| @code{}setsid steam steam://open/bigpicture@endcode  \|
    \| Image                        \| @code{}steam.png@endcode                             \|
  }
  @tab{macOS | <!-- -->
    \| Field                        \| Value                                          \|
    \|------------------------------\|------------------------------------------------\|
    \| Application Name             \| @code{}Steam Big Picture@endcode               \|
    \| Command Preporations -> Undo \| @code{}open steam://close/bigpicture@endcode   \|
    \| Detached Commands            \| @code{}open steam://open/bigpicture@endcode    \|
    \| Image                        \| @code{}steam.png@endcode                       \|
  }
  @tab{Windows | <!-- -->
    \| Field                        \| Value                                     \|
    \|------------------------------\|-------------------------------------------\|
    \| Application Name             \| @code{}Steam Big Picture@endcode          \|
    \| Command Preporations -> Undo \| @code{}steam://close/bigpicture@endcode   \|
    \| Detached Commands            \| @code{}steam://open/bigpicture@endcode    \|
    \| Image                        \| @code{}steam.png@endcode                  \|
  }
}

### Epic Game Store game

> [!NOTE]
> Using the URI method will be the most consistent between various games.

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

> [!NOTE]
> Using the URI method will be the most consistent between various games.

#### URI

@tabs{
  @tab{FreeBSD | <!-- -->
    \| Field             \| Value                                                \|
    \|-------------------\|------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                        \|
    \| Detached Commands \| @code{}setsid steam steam://rungameid/464920@endcode \|
  }
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
  @tab{FreeBSD | <!-- -->
    \| Field             \| Value                                                        \|
    \|-------------------\|--------------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                                \|
    \| Command           \| @code{}MarsSteam@endcode                                     \|
    \| Working Directory \| @code{}~/.steam/steam/SteamApps/common/Survivng Mars@endcode \|
  }
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
  @tab{FreeBSD | <!-- -->
    \| Field             \| Value                                                                  \|
    \|-------------------\|------------------------------------------------------------------------\|
    \| Application Name  \| @code{}Surviving Mars@endcode                                          \|
    \| Command           \| @code{}~/.steam/steam/SteamApps/common/Survivng Mars/MarsSteam@endcode \|
  }
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

> [!TIP]
> The above only works if the xrandr mode already exists. You will need to create new modes to stream to macOS
> and iOS devices, since they use non-standard resolutions.
>
> You can update the ``Do`` command to this:
> ```bash
> bash -c "${HOME}/scripts/set-custom-res.sh \"${SUNSHINE_CLIENT_WIDTH}\" \"${SUNSHINE_CLIENT_HEIGHT}\" \"${SUNSHINE_CLIENT_FPS}\""
> ```
>
> The `set-custom-res.sh` will have this content:
> ```bash
> #!/bin/bash
> set -e
>
> # Get params and set any defaults
> width=${1:-1920}
> height=${2:-1080}
> refresh_rate=${3:-60}
>
> # You may need to adjust the scaling differently so the UI/text isn't too small / big
> scale=${4:-0.55}
>
> # Get the name of the active display
> display_output=$(xrandr | grep " connected" | awk '{ print $1 }')
>
> # Get the modeline info from the 2nd row in the cvt output
> modeline=$(cvt ${width} ${height} ${refresh_rate} | awk 'FNR == 2')
> xrandr_mode_str=${modeline//Modeline \"*\" /}
> mode_alias="${width}x${height}"
>
> echo "xrandr setting new mode ${mode_alias} ${xrandr_mode_str}"
> xrandr --newmode ${mode_alias} ${xrandr_mode_str}
> xrandr --addmode ${display_output} ${mode_alias}
>
> # Reset scaling
> xrandr --output ${display_output} --scale 1
>
> # Apply new xrandr mode
> xrandr --output ${display_output} --primary --mode ${mode_alias} --pos 0x0 --rotate normal --scale ${scale}
>
> # Optional reset your wallpaper to fit to new resolution
> # xwallpaper --zoom /path/to/wallpaper.png
> ```

###### Wayland (wlroots, e.g. hyprland)

| Prep Step | Command                                                                                                                                  |
|-----------|------------------------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}sh -c "wlr-xrandr --output HDMI-1 --mode \"${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}@${SUNSHINE_CLIENT_FPS}Hz\""@endcode |
| Undo      | @code{}wlr-xrandr --output HDMI-1 --mode 3840x2160@120Hz@endcode                                                                         |

> [!TIP]
> `wlr-xrandr` only works with wlroots-based compositors.

###### Gnome (X11)

| Prep Step | Command                                                                                                                               |
|-----------|---------------------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}sh -c "xrandr --output HDMI-1 --mode ${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT} --rate ${SUNSHINE_CLIENT_FPS}"@endcode |
| Undo      | @code{}xrandr --output HDMI-1 --mode 3840x2160 --rate 120@endcode                                                                     |

###### Gnome (Wayland)

| Prep Step | Command                                                                                                                                                                                               |
|-----------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}sh -c "displayconfig-mutter set --connector HDMI-1 --resolution ${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT} --refresh-rate ${SUNSHINE_CLIENT_FPS} --hdr ${SUNSHINE_CLIENT_HDR}"@endcode |
| Undo      | @code{}displayconfig-mutter set --connector HDMI-1 --resolution 3840x2160 --refresh-rate 120 --hdr false@endcode                                                                                      |

Installation instructions for displayconfig-mutter can be [found here](https://github.com/eaglesemanation/displayconfig-mutter). Alternatives include
[gnome-randr-rust](https://github.com/maxwellainatchi/gnome-randr-rust) and [gnome-randr.py](https://gitlab.com/Oschowa/gnome-randr), but both of those are
unmaintained and do not support newer Mutter features such as HDR and VRR.

> [!TIP]
> HDR support has been added to Gnome 48, to check if your display supports it, you can run this:
> ```
> displayconfig-mutter list
> ```
> If it doesn't, then remove ``--hdr`` flag from both ``Do`` and ``Undo`` steps.

###### KDE Plasma (Wayland, X11)

| Prep Step | Command                                                                                                                              |
|-----------|--------------------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}sh -c "kscreen-doctor output.HDMI-A-1.mode.${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}@${SUNSHINE_CLIENT_FPS}"@endcode |
| Undo      | @code{}kscreen-doctor output.HDMI-A-1.mode.3840x2160@120@endcode                                                                     |

> [!CAUTION]
> The names of your displays will differ between X11 and Wayland.
> Be sure to use the correct name, depending on your session manager.
> e.g., On X11, the monitor may be called ``HDMI-A-0``, but on Wayland, it may be called ``HDMI-A-1``.

> [!TIP]
> Replace ``HDMI-A-1`` with the display name of the monitor you would like to use for Moonlight.
> You can list the monitors available to you with:
> ```
> kscreen-doctor -o
> ```
>
> These will also give you the supported display properties for each monitor. You can select them either by
> hard-coding their corresponding number (e.g. ``kscreen-doctor output.HDMI-A1.mode.0``) or using the above
> ``do`` command to fetch the resolution requested by your Moonlight client
> (which has a chance of not being supported by your monitor).

###### NVIDIA

| Prep Step | Command                                                                                                                                                                                                                        |
|-----------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}sh -c "nvidia-settings -a CurrentMetaMode=\"HDMI-1: nvidia-auto-select { ViewPortIn=${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}, ViewPortOut=${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}+0+0 }\""@endcode |
| Undo      | @code{}nvidia-settings -a CurrentMetaMode=\"HDMI-1: nvidia-auto-select { ViewPortIn=3840x2160, ViewPortOut=3840x2160+0+0 }"@endcode                                                                                            |

##### macOS

###### displayplacer

> [!NOTE]
> This example uses the `displayplacer` tool to change the resolution.
> This tool can be installed following instructions in their
> [GitHub repository](https://github.com/jakehilborn/displayplacer).

| Prep Step | Command                                                                                                                                                                  |
|-----------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}sh -c "displayplacer \"id:<screenId> res:${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT} hz:${SUNSHINE_CLIENT_FPS} scaling:on origin:(0,0) degree:0\""@endcode |
| Undo      | @code{}displayplacer "id:<screenId> res:3840x2160 hz:120 scaling:on origin:(0,0) degree:0"@endcode                                                                       |

##### Windows
Sunshine has built-in support for changing the resolution and refresh rate on Windows. If you prefer to use a
third-party tool, you can use *QRes* as an example.

###### QRes

> [!NOTE]
> This example uses the *QRes* tool to change the resolution and refresh rate.
> This tool can be downloaded from their [SourceForge repository](https://sourceforge.net/projects/qres).

| Prep Step | Command                                                                                                                   |
|-----------|---------------------------------------------------------------------------------------------------------------------------|
| Do        | @code{}cmd /C "FullPath\qres.exe /x:%SUNSHINE_CLIENT_WIDTH% /y:%SUNSHINE_CLIENT_HEIGHT% /r:%SUNSHINE_CLIENT_FPS%"@endcode |
| Undo      | @code{}FullPath\qres.exe /x:3840 /y:2160 /r:120@endcode                                                                   |

### Additional Considerations

#### Linux (Flatpak)

> [!CAUTION]
> Because Flatpak packages run in a sandboxed environment and do not normally have access to the
> host, the Flatpak of Sunshine requires commands to be prefixed with `flatpak-spawn --host`.

#### Windows
**Elevating Commands (Windows)**

If you've installed Sunshine as a service (default), you can specify if a command should be elevated with
administrative privileges. Simply enable the elevated option in the WEB UI, or add it to the JSON configuration.
This is an option for both prep-cmd and regular commands and will launch the process with the current user without a
UAC prompt.

**Example**
```json
{
  "name": "Game With AntiCheat that Requires Admin",
  "output": "",
  "cmd": "ping 127.0.0.1",
  "exclude-global-prep-cmd": false,
  "elevated": true,
  "prep-cmd": [
    {
      "do": "powershell.exe -command \"Start-Streaming\"",
      "undo": "powershell.exe -command \"Stop-Streaming\"",
      "elevated": false
    }
  ],
  "image-path": ""
}
```

<div class="section_buttons">

| Previous                          |                                    Next |
|:----------------------------------|----------------------------------------:|
| [Configuration](configuration.md) | [Awesome-Sunshine](awesome_sunshine.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
