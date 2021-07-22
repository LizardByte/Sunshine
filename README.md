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

### Requirements:
Ubuntu 20.04:
Install the following
```
sudo apt install cmake gcc-10 g++-10 libssl-dev libavdevice-dev libboost-thread-dev libboost-filesystem-dev libboost-log-dev libpulse-dev libopus-dev libxtst-dev libx11-dev libxrandr-dev libxfixes-dev libevdev-dev libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev
```

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

### Trouleshooting:
- If you get "Could not create Sunshine Gamepad: Permission Denied", ensure you are part of the group "input":
	- `groups $USER`
	
- If Sunshine sends audio from the microphone instead of the speaker, try the following steps:
 	1. `$ pacmd list-sources | grep "name:"` or `$ pactl info | grep Source` if running pipewire.
	2. Copy the name to the configuration option "audio_sink"
	3. restart sunshine

## Windows 10

### Requirements:

	mingw-w64-x86_64-openssl mingw-w64-x86_64-cmake mingw-w64-x86_64-toolchain mingw-w64-x86_64-opus mingw-w64-x86_64-x265 mingw-w64-x86_64-boost git mingw-w64-x86_64-make

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
	- Type in the username and password shown the first time you run Sunshine
	- Go to "PIN" in the Header
	- Type in your PIN and press Enter, you should get a Success Message
- Click on one of the Applications listed
- Have fun :)


## Note:
- The Windows key is not passed through by Moonlight, therefore Sunshine maps Right-Alt key to the Windows key
- If you set Video Bitrate to 0.5Mb/s:
	- Sunshine will use CRF or QP to controll the quality of the stream. (See example configuration file for more details)
	- This is less CPU intensive and it has lower average bandwith requirements compared to manually setting bitrate to acceptable quality
	- However, it has higher peak bitrates, forcing Sunshine to drop entire frames when streaming 1080P due to their size.
	- When this happens, the video portion of the stream appears to be frozen.
	- This is rare enough that using this for the desktop environment is tolerable (in my opinion), however for gaming not so much.


## Credits:
- [Simple-Web-Server](https://gitlab.com/eidheim/Simple-Web-Server)
- [Moonlight](https://github.com/moonlight-stream)
- [Looking-Glass](https://github.com/gnif/LookingGlass) (For showing me how to properly capture frames on Windows, saving me a lot of time :)
- [Eretik](http://eretik.omegahg.com/) (For creating PolicyConfig.h, allowing me to change the default audio device on Windows programmatically)

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
