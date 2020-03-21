######### Linux ##############

Requirements:
	Ubuntu 19.10: cmake libssl-dev libavdevice-dev libboost-thread-dev libboost-filesystem-dev libboost-log-dev libpulse-dev libopus-dev libxtst-dev libx11-dev libxfixes-dev libevdev-dev libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev

Compilation:
	* git clone https://github.com/loki-47-6F-64/sunshine.git --recurse-submodules
	* cd sunshine && mkdir build && cd build
	* cmake ..
	* make


Setup:
	* sunshine needs access to uinput to create mouse and gamepad events:
		* Add user to group 'input': "usermod -a -G input username
		* Create a file: "/etc/udev/rules.d/85-sunshine-input.rules"
		* The contents of the file is as follows:
			KERNEL=="uinput", GROUP="input", mode="0660"
	* assets/sunshine.conf is an example configuration file. Modify it as you see fit and use it by running: "sunshine path/to/sunshine.conf"
	* path/to/build/dir/sunshine.service is used to start sunshine in the background:
		* cp sunshine.service $HOME/.config/systemd/user/
		* Modify $HOME/.config/systemd/user/sunshine.conf to point to the sunshine executable
		* systemctl --user start sunshine

	* assets/apps.json is an example of a list of applications that are started just before running a stream:
		* See below for a detailed explanation

Trouleshooting:
	* If you get "Could not create Sunshine Gamepad: Permission Denied", ensure you are part of the group "input":
		* groups
	* If Sunshine sends audio from the microphone instead of the speaker, try the following steps:
		* pacmd list-sources | grep "name:"
		* Copy the name to the configuration option "audio_sink"
		* restart sunshine



######### Windows 10 ############

Requirements:
	MSYS2 : mingw-w64-x86_64-openssl mingw-w64-x86_64-cmake mingw-w64-x86_64-toolchain mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-boost

Compilation:
	* git clone https://github.com/loki-47-6F-64/sunshine.git --recurse-submodules
	* cd sunshine && mkdir build && cd build
	* cmake -G"Unix Makefiles" ..
	* make

Setup:
	* <optional> Gamepad support: Download and run 'ViGEmBus_Setup_1.16.116.exe' from [https://github.com/ViGEm/ViGEmBus/releases]

== Static build ==
Requirements:
	MSYS2 : mingw-w64-x86_64-openssl mingw-w64-x86_64-cmake mingw-w64-x86_64-toolchain mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-boost git-lfs

Compilation:
	* git lfs install
	* git clone https://github.com/loki-47-6F-64/sunshine.git --recurse-submodules
	* cd sunshine && mkdir build && cd build
	* cmake -DSUNSHINE_STANDALONE=ON -DSUNSHINE_ASSETS_DIR=assets -G"Unix Makefiles" ..
	* make



######### Common #############

Usage:
	* run "sunshine path/to/sunshine.conf"
	* In Moonlight: Add PC manually
	* When Moonlight request you insert the correct pin on sunshine:
		wget xxx.xxx.xxx.xxx:47989/pin/xxxx -- where the first few x's are substituted by the ip of Sunshine and the final 4 x'es are substituted by the pin
			or
		Type in the URL bar of your browser: xxx.xxx.xxx.xxx:47989/pin/xxxx -- where the first few x's are substituted by the ip of the final 4 x'es are subsituted by the pin
	* Click on one of the Applications listed
	* Have fun :)


Note:
	* The Windows key is not passed through by Moonlight, therefore Sunshine maps Right-Alt key to the Windows key
	* If you set Video Bitrate to 0.5Mb/s:
		* Sunshine will use CRF or QP to controll the quality of the stream. (See example configuration file for more details)
		* This is less CPU intensive and it has lower average bandwith requirements compared to manually setting bitrate to acceptable quality
		* However, it has higher peak bitrates, forcing Sunshine to drop entire frames when streaming 1080P due to their size.
		* When this happens, the video portion of the stream appears to be frozen.
		* This is rare enough that using this for the desktop environment is tolerable (in my opinion), however for gaming not so much.


Credits:
	* Simple-Web-Server [https://gitlab.com/eidheim/Simple-Web-Server]
	* Moonlight [https://github.com/moonlight-stream]
	* Looking-Glass [https://github.com/gnif/LookingGlass] (For showing me how to properly capture frames on Windows, saving me a lot of time :)



Application List:
	* You can use Environment variables in place of values
		* $(HOME) will be replaced by the value of $HOME
		* $$ will be replaced by $ --> $$(HOME) will be replaced by $(HOME)
	* env: Adds or overwrites Environment variables for the commands/applications run by Sunshine.
		* "Variable name":"Variable value"
	* apps: The list of applications
		* name: Self explanatory
		* output <optional>: The file where the output of the command is stored
			* If it is not specified, the output is ignored
		* prep-cmd: A list of commands to be run before/after the application
			* If any of the prep-commands fail, starting the application is aborted
			* do: Run before the application
				* If it fails, all 'undo' commands of the previously succeeded 'do' commands are run
			* undo <optional>: Run after the application has terminated
				* This should not fail considering it is supposed to undo the 'do' commands.
				* If it fails, Sunshine is terminated
		* cmd <optional>: The main application
			* If not specified, a processs is started that sleeps indefinitely

When an application is started, if there is an application already running, it will be terminated.
When the application has been shutdown, the stream shuts down as well.
In addition to the apps listed, one app "Desktop" is hardcoded into Sunshine. It does not start an application, instead it simply starts a stream.
