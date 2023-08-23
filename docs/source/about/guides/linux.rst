Linux
======
Collection of Sunshine Linux host guides.

Remote SSH Headless Setup
-------------------------
Author: Eric Dong

This is a guide to setup remote SSH into host to startup X server and sunshine without physical login and dummy plug.

.. Attention:: This guide is specific for Xorg and NVidia GPUs. I start the Xserver using the ``startx`` command. I also only tested this on an Artix runit init system on LAN. I didn't have to do anything special with pulseaudio. Not sure if pipewire works.

The Big Picture
^^^^^^^^^^^^^^^
Once you are done, you will need to perform these there steps:

1. Turn on the host machine
2. Start sunshine on remote host with a script that:
	- Edits permissions of ``/dev/uinput`` (added sudo config to execute script with no password prompt)
	- Starts X Server with ``startx``
	- Starts ``Sunshine`` 
3. Startup Moonlight on the client of interest and connect to host

First we will setup the host and then the SSH Client (Which may not be the same as the machine running the moonlight client)

Host Setup
^^^^^^^^^^

We will be setting up:

1. Static IP setup
2. Disable PAM in sshd
3. Virtual Display Acceleration via NVIDIA's TwinView X11 Config
4. Script for ``uinput`` permission workaround
5. Script to put everything together to startup X server and Sunshine

Static IP Setup
+++++++++++++++
Setup static IP Address for host. For LAN connections you can use DHCP reservation within your assigned range 192.168.x.x. This will allow you to ssh to the host consistently, so the assigned IP address does not change.

Disabling PAM in sshd
+++++++++++++++++++++
I noticed when the ssh session is disconnected for any reason, `pulseaudio` would disconnect. This is due to PAM handling sessions. When running `dmesg`, I noticed `elogind` would say removed user session.

According to this `article <https://devicetests.com/ssh-usepam-security-session-status>`_ disabling PAM increases security, but reduces certain functionality in terms of session handling. 
*Do so at your own risk!*

Reference:
https://forums.gentoo.org/viewtopic-t-1090186-start-0.html

Virtual Display Setup
+++++++++++++++++++++
This is only available for NVidia GPUs

.. code-block::  

	Section "ServerLayout"
		Identifier     "TwinLayout"
		Screen         0 "metaScreen" 0 0
	EndSection

	Section "Monitor"
		Identifier     "Monitor0"
		Option         "Enable" "true"
	EndSection

	Section "Device"
		Identifier     "Card0"
		Driver         "nvidia"
		VendorName     "NVIDIA Corporation"

		#refer to the link below for more information on each of the following options.
		Option         "MetaModes"          "1920x1080"
		Option         "ConnectedMonitor"   "DP-0"
		Option         "ModeValidation" "NoDFPNativeResolutionCheck,NoVirtualSizeCheck,NoMaxPClkCheck,NoHorizSyncCheck,NoVertRefreshCheck,NoWidthAlignmentCheck"
	EndSection

	Section "Screen"
		Identifier     "metaScreen"
		Device         "Card0"
		Monitor        "Monitor0"
		DefaultDepth    24
		Option         "TwinView" "True"
		SubSection "Display"
			Modes          "1920x1080"
		EndSubSection
	EndSection

The ``ConnectedMonitor`` tricks the GPU into thinking a monitor is connected, even if there is none actually connected! This allows a virtual display to be created that is accelerated with your GPU! The ``ModeValidation`` option disables valid resolution checks, so you can choose any resolution on the host!


UINPUT Workaround
++++++++++++++++++

*Script*
Two scripts will need to be written to get this setup
1. Script to update permissions on ``/dev/uinput``. Since we aren't logged into the host, the udev rule doesn't apply.
2. Script to start up Xserver and sunshine

*Setup Script*
We will manually change the permissions of ``/dev/uinput`` using ``chown``. You need to use ``sudo`` to make this change, so add/update the entry in ``/etc/sudoers.d/<user>``
.. code-block::

	<user> ALL=(ALL:ALL) ALL, NOPASSWD: /home/<user>/scripts/sunshine-setup.sh

These changes allow the script to use sudo without being prompted with a password.


Everything Together Now!
+++++++++++++++++++++++++


*sunshine-setup.sh*

.. code-block:: sh

	#!/bin/bash
	chown <user>:<user> /dev/uinput

	# Optional
	# blocks wifi, so ethernet is used
	# use rfkill list to get the id of the Wiresless LAN
	# rfkill block <wireless_lan_index>

*Sunshine Startup Script*

.. code-block:: sh

	#!/bin/bash

	export DISPLAY=:0

	# Check existing X server
	ps -e | grep X >/dev/null
	[[ ${?} -ne 0 ]] && {
		echo "Starting Xserver"
		startx &>/dev/null &
	} || echo "Xserver already running"

	# Check if sunshine is already running
	ps -e | grep -e .*sunshine$ >/dev/null
	[[ ${?} -ne 0 ]] && {
		sudo ~/scripts/update-udev.sh
		sleep 1
		echo "Starting Sunshine!"
		sunshine >/dev/null
		echo "test"
		pkill -ef sunshine
		pkill -ef X
	} || echo "Sunshine is already running"

SSH Client Setup
^^^^^^^^^^^^^^^^

We will be setting up:

1. SSH key generation
2. Script to SSH into host to execute sunshine script from Host Setup in step 4.

SSH Key Authentication Setup
+++++++++++++++++++++++++++++

1. Setup your SSH keys with ``ssh-keygen`` and use ``ssh-copy-id`` to authorize remote login to your host. Run ``ssh <user>@<ip_address>`` to login to your host. SSH keys automate login so you don't need to input your password!
2. Optionally setup a ``~/.ssh/config`` file to simplify the ``ssh`` command
   .. code-block::

		Host <some_alias>
			Hostname <ip_address>
			User <username>
			IdentityFile ~/.ssh/<your_private_key>

   Now you can use ``ssh <some_alias>``.  
   ``ssh <some_alias> <commands/script>`` will execute the command or script on the remote host.

SSH Script
++++++++++
This bash script will automate the startup of the Xserver and Sunshine on the host.
This can be run on linux / macOS system.
On Windows, this can be run inside a ``git-bash``

For Android/IOS you can install linux emulators. E.g. ``Userland`` for Android and ``ISH`` for IOS 

.. code-block:: sh

	#!/bin/bash

	ssh_args="eric@192.168.1.3"

	check_host(){
	  result=1
	  while [[ $result -ne 0 ]]
	  do
	    echo "checking host..."
		ssh $ssh_args "exit 0" 2>/dev/null
		result=$?
		[[ $result -ne 0 ]] && echo "Failed to ssh to $ssh_args, with exit code $result"
		  sleep 2
	  done
	  echo "Host is ready for streaming!"
	}

	start_stream(){
	  echo "Starting sunshine server on host..."
	  echo "Start moonlight on your client of choice"
	  ssh $ssh_args "~/scripts/sunshine.sh &" 
	}

	cleanup(){
	  ssh $ssh_args "pkill -ef sunshine"
	  ssh $ssh_args "pkill -ef X"
	}

	check_host
	start_stream

	# Doing ctrl + c will continue the script and activate the cleanup
	#cleanup

