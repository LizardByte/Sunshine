![Sunshine icon](gamepad.png "Sunshine")
# Introduction
Sunshine is a Gamestream host for Moonlight

[![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/cgrtw2g3fq9b0b70/branch/master?svg=true)](https://ci.appveyor.com/project/loki-47-6F-64/sunshine/branch/master)
[![Downloads](https://img.shields.io/github/downloads/Loki-47-6F-64/sunshine/total)](https://github.com/Loki-47-6F-64/sunshine/releases)

- [Building](README.md#building)
- [Credits](README.md#credits)

# Building
- [Linux](README.md#linux)
- [Windows](README.md#windows-10)

## Linux

If you do not wish to clutter your PC with development files, yet you want the very latest version...
You can use these [build scripts](scripts/README.md)
They make use of docker to handle building Sunshine automatically

### Requirements:

Ubuntu 20.04:
Install the following:

#### Common
```
sudo apt install cmake gcc-10 g++-10 libssl-dev libavdevice-dev libboost-thread-dev libboost-filesystem-dev libboost-log-dev libpulse-dev libopus-dev libevdev-dev
```
#### X11
```
sudo apt install libxtst-dev libx11-dev libxrandr-dev libxfixes-dev libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev
```

#### KMS
This requires additional [setup](README.md#Setup).
```
sudo apt install libdrm-dev libcap-dev
```

#### Wayland
This is for wlroots based compositores, such as Sway
```
sudo apt install libwayland-dev
```

#### Cuda + NvFBC
This requires proprietary software
On Ubuntu 20.04, the cuda compiler will fail since it's version is too old, it's recommended you compile the sources with the [build scripts](scripts/README.md)
```
sudo apt install nvidia-cuda-dev nvidia-cuda-toolkit
```

#### Warning:
You might require ffmpeg version >= 4.3. Check the troubleshooting section for more information.

### Compilation:
- `git clone https://github.com/loki-47-6F-64/sunshine.git --recurse-submodules`
- `cd sunshine && mkdir build && cd build`
- `cmake -DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10 ..`
- `make -j ${nproc}`

### Setup:
sunshine needs access to uinput to create mouse and gamepad events:

- Add user to group 'input':
	`usermod -a -G input $USER`
- Create udev rules:
	- Run the following command: 
	`nano /etc/udev/rules.d/85-sunshine-input.rules`
	- Input the following contents:
	`KERNEL=="uinput", GROUP="input", MODE="0660"`
	- Save the file and exit
		1. `CTRL+X` to start exit
		2. `Y` to save modifications
- `assets/sunshine.conf` is an example configuration file. Modify it as you see fit, then use it by running: 
	`sunshine path/to/sunshine.conf`
- Configure autostart service
	`path/to/build/dir/sunshine.service` is used to start sunshine in the background. To use it, do the following:
	1. Copy it to the users systemd, `cp sunshine.service ~/.config/systemd/user/`
	2. Starting
		- Onetime: 
			`systemctl --user start sunshine`
		- Always on boot:
			`systemctl --user enable sunshine`

- `assets/apps.json` is an [example](README.md#application-list) of a list of applications that are started just before running a stream

#### Additional Setup for KMS:
Please note that `cap_sys_admin` may as well be root, except you don't need to be root to run it.
It's necessary to allow Sunshine to use KMS
- `sudo setcap cap_sys_admin+p sunshine`

### Trouleshooting:
- If you get "Could not create Sunshine Gamepad: Permission Denied", ensure you are part of the group "input":
	- `groups $USER`
	
- If Sunshine sends audio from the microphone instead of the speaker, try the following steps:
	1. Check whether you're using Pulseaudio or Pipewire
		- Pulseaudio: Use `pacmd list-sources | grep "name:"`
		- Pipewire: Use `pactl info | grep Source`. In some causes you'd need to use the `sink` device. Try `pactl info | grep Sink`, if _Source_ doesn't work.
	2. Copy the name to the configuration option "audio_sink"
	3. Restart sunshine

- If you get "Error: Failed to create client: Daemon not running", ensure that your avahi-daemon is running:
	- `systemctl status avahi-daemon`

- If you use hardware acceleration on Linux using an Intel or an AMD GPU (with VAAPI), you will get tons of [graphical issues](https://github.com/loki-47-6F-64/sunshine/issues/228) if your ffmpeg version is < 4.3. If it is not available in your distribution's repositories, consider using a newer version of your distribution.
	- Ubuntu started to ship ffmpeg 4.3 starting with groovy (20.10). If you're using an older version, you could use [this PPA](https://launchpad.net/%7Esavoury1/+archive/ubuntu/ffmpeg4) instead of upgrading. **Using PPAs is dangerous and may break your system. Use it at your own risk.**

## Windows 10

### Requirements:

First you need to install [MSYS2](https://www.msys2.org), then startup "MSYS2 MinGW 64-bit" and install the following packages using `pacman -S`:

	mingw-w64-x86_64-binutils mingw-w64-x86_64-openssl mingw-w64-x86_64-cmake mingw-w64-x86_64-toolchain mingw-w64-x86_64-opus mingw-w64-x86_64-x265 mingw-w64-x86_64-boost git mingw-w64-x86_64-make cmake make gcc

### Compilation:
- `git clone https://github.com/loki-47-6F-64/sunshine.git --recursive`
- `cd sunshine && mkdir build && cd build`
- `cmake -G"Unix Makefiles" ..`
- `mingw32-make`

### Setup:
- **OPTIONAL** Gamepad support: Download and run 'ViGEmBus_Setup_1.16.116.exe' from [https://github.com/ViGEm/ViGEmBus/releases]



# Common 

## Usage:
- run "sunshine path/to/sunshine.conf"
- If running for the first time, make sure to note the username and password Sunshine showed to you, since you **cannot get back later**!
- In Moonlight: Add PC manually
- When Moonlight request you insert the correct pin on sunshine:
	- Type in the URL bar of your browser: `https://xxx.xxx.xxx.xxx:47990` where `xxx.xxx.xxx.xxx` is the IP address of your computer
	- Ignore any warning given by your browser about "insecure website"
	- You should compile the next page with a new username and a password, needed to login into the next step
	- Press "Save" and log in using the credentials given above 
	- Go to "PIN" in the Header
	- Type in your PIN and press Enter, you should get a Success Message
- Click on one of the Applications listed
- Have fun :)

## Shortcuts:

All shortcuts start with CTRL + ALT + SHIFT, just like Moonlight
- CTRL + ALT + SHIFT + N --> Hide/Unhide the cursor (This may be usefull for Remote Desktop Mode for Moonlight)
- CTRL + ALT + SHIFT + F1/F13 --> Switch to different monitor for Streaming

## Credits:
- [Simple-Web-Server](https://gitlab.com/eidheim/Simple-Web-Server)
- [Moonlight](https://github.com/moonlight-stream)
- [Looking-Glass](https://github.com/gnif/LookingGlass) (For showing me how to properly capture frames on Windows, saving me a lot of time :)
- [Eretik](http://eretik.omegahg.com/) (For creating PolicyConfig.h, allowing me to change the default audio device on Windows programmatically)
- [Twitter emoji](https://github.com/twitter/twemoji/blob/master/LICENSE-GRAPHICS) (Sunshine's icon is made of twemoji)

## Application List:
**Note:** You can change the Application List in the "Apps" section of the User Interface `https://xxx.xxx.xxx.xxx:47990/`
- You can use Environment variables in place of values
	- $(HOME) will be replaced by the value of $HOME
	- $$ will be replaced by $ --> $$(HOME) will be replaced by $(HOME)
- env: Adds or overwrites Environment variables for the commands/applications run by Sunshine.
	- "Variable name":"Variable value"
- apps: The list of applications
	- Example:
	```json
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
	```
	- name: Self explanatory
	- output <optional>: The file where the output of the command is stored
		- If it is not specified, the output is ignored
	- detached: A list of commands to be run and forgotten about
	- prep-cmd: A list of commands to be run before/after the application
		- If any of the prep-commands fail, starting the application is aborted
		- do: Run before the application
			- If it fails, all 'undo' commands of the previously succeeded 'do' commands are run
		- undo <optional>: Run after the application has terminated
			- This should not fail considering it is supposed to undo the 'do' commands.
			- If it fails, Sunshine is terminated
	- cmd <optional>: The main application
		- If not specified, a processs is started that sleeps indefinitely

1. When an application is started, if there is an application already running, it will be terminated.
2. When the application has been shutdown, the stream shuts down as well.
3. In addition to the apps listed, one app "Desktop" is hardcoded into Sunshine. It does not start an application, instead it simply starts a stream.

Linux
```json
{
	"env":{ 
		"DISPLAY":":0",
		"DRI_PRIME":"1",
		"XAUTHORITY":"$(HOME)/.Xauthority",
		"PATH":"$(PATH):$(HOME)/.local/bin"
	},
	"apps":[
	{
		"name":"Low Res Desktop",
		"prep-cmd":[
		{ "do":"xrandr --output HDMI-1 --mode 1920x1080", "undo":"xrandr --output HDMI-1 --mode 1920x1200" }
		]
	},
	{
		"name":"Steam BigPicture",

		"output":"steam.txt",
		"cmd":"steam -bigpicture",
		"prep-cmd":[]
	}
	]
}
```
Windows
```json
{
	"env":{
		"PATH":"$(PATH);C:\\Program Files (x86)\\Steam"
	},
	"apps":[
	{
		"name":"Steam BigPicture",

		"output":"steam.txt",
		"prep-cmd":[
			{"do":"steam \"steam://open/bigpicture\""}
		]
	}
	]
}
```
