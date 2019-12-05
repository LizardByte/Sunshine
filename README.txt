Requirements:
	Ubuntu 19.10: cmake libssl-dev libavdevice-dev libboost-thread-dev libpulse-dev libopus-dev libfixes-dev libxtst-dev libx11-dev

Compilation:
	* git clone <repository> --recurse-submodules
	* mkdir build && cd build
	* cmake -DCMAKE_BUILD_TYPE=Release ..
	* make


Usage:
	* run sunshine
	* In Moonlight: Add PC manually
	* When Moonlight request you insert the correct pin on sunshine:
		wget xxx.xxx.xxx.xxx:47989/pin/xxxx -- where the final 4 x'es are subsituted by the pin
			or
		Type in the URL bar of your browser: xxx.xxx.xxx.xxx:47989/pin/xxxx -- where the final 4 x'es are subsituted by the pin
	* Click on one of the Applications listed
	* Have fun :)

Note:
	* The Windows key is not passed through by Moonlight, therefore Sunshine maps Right-Alt key to the Windows key
