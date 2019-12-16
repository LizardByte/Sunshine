Requirements:
	Ubuntu 19.10: cmake libssl-dev libavdevice-dev libboost-thread-dev libpulse-dev libopus-dev libfixes-dev libxtst-dev libx11-dev libevdev-dev

Compilation:
	* git clone <repository> --recurse-submodules
	* mkdir build && cd build
	* cmake -DCMAKE_BUILD_TYPE=Release ..
	* make


Setup:
	* assets/sunshine.conf is an example configuration file. Modify it as you see fit and use it by running: "sunshine path/to/sunshine.conf"
	* assets/sunshine.service is used to start sunshine in the background:
		* cp sunshine.service $HOME/.config/systemd/user/
		* Modify $HOME/.config/systemd/user/sunshine.conf to point to the sunshine executable
		* systemctl --user start sunshine

	* assets/apps.json is an example of a list of applications that are started just before running a stream:
		* See below for a detailed explanation

Usage:
	* run "sunshine"
	* In Moonlight: Add PC manually
	* When Moonlight request you insert the correct pin on sunshine:
		wget xxx.xxx.xxx.xxx:47989/pin/xxxx -- where the final 4 x'es are subsituted by the pin
			or
		Type in the URL bar of your browser: xxx.xxx.xxx.xxx:47989/pin/xxxx -- where the final 4 x'es are subsituted by the pin
	* Click on one of the Applications listed
	* Have fun :)


Note:
	* The Windows key is not passed through by Moonlight, therefore Sunshine maps Right-Alt key to the Windows key


Credits:
	* Simple-Web-Server [https://gitlab.com/eidheim/Simple-Web-Server]
	* Moonlight [https://github.com/moonlight-stream]



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
