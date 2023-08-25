Linux
======

Collection of Sunshine Linux host guides.

Remote SSH Headless Setup
-------------------------
Author: *Eric Dong*  

Difficulty: *Intermediate*

This is a guide to setup remote SSH into host to startup X server and sunshine without physical login and dummy plug.
The virtual display is accelerated by the NVidia GPU using the TwinView configuration.

.. Attention::
	This guide is specific for Xorg and NVidia GPUs. I start the X server using the ``startx`` command.
	I also only tested this on an Artix runit init system on LAN.
	I didn't have to do anything special with pulseaudio (pipewire untested).

.. tip:: 
	Prior to editing any system configurations, you should make a copy of the original file.
	This will allow you to use it for reference or revert your changes easily.

The Big Picture
^^^^^^^^^^^^^^^
Once you are done, you will need to perform these 3 steps:

#. Turn on the host machine
#. Start sunshine on remote host with a script that:
	- Edits permissions of ``/dev/uinput`` (added sudo config to execute script with no password prompt)
	- Starts X server with ``startx``
	- Starts ``Sunshine`` 
#. Startup Moonlight on the client of interest and connect to host

.. admonition:: Alternative to SSH
	:class: seealso

	**Step 2** can be replaced with autologin and starting sunshine as a service or putting ``sunshine &`` in your ``.xinitrc`` file 
	if you start your X server with ``startx``.
	In this case workaround for ``/dev/uinput`` permissions is not needed because the udev rule would be triggered for "physical" login.
	See :ref:`Linux Setup <about/usage:linux>`. I personally think autologin compromises the security of the PC, so I went with the remote SSH route.
	I use the PC more than for gaming, so I don't need a virtual display everytime I turn on the PC (E.g running updates, config changes, file/media server).

First we will setup the host and then the SSH Client (Which may not be the same as the machine running the moonlight client)

Host Setup
^^^^^^^^^^

We will be setting up:

#. `Static IP Setup <static ip setup_>`_
#. `SSH Server setup <ssh server setup_>`_
#. `Virtual Display Acceleration via NVIDIA's TwinView X11 Config <virtual display setup_>`_
#. `Script for uinput permission workaround <uinput workaround_>`_
#. `Script to put everything together to startup X server and Sunshine <putting everything together_>`_


Static IP Setup
+++++++++++++++
Setup static IP Address for host. For LAN connections you can use DHCP reservation within your assigned range 
192.168.x.x. This will allow you to ssh to the host consistently, so the assigned IP address does not change.

SSH Server setup
++++++++++++++++

.. note:: Most distros have OpenSSH already installed. If it is not present, install OpenSSH using your package manager.

.. tab:: Ubuntu

	.. code-block:: sh

		sudo apt update
		sudo apt install openssh-server


.. tab:: Arch

	.. code-block:: sh

		sudo pacman -S openssh

.. important::
	If you are using runit, you will also need to install ``openssh-runit``
	and run 
	
	``ln -s /etc/runit/sv/sshd /run/runit/service``


**Disabling PAM in sshd**

.. tip::
	Run the command to check the ssh configuration prior to restarting the sshd service.

	``sudo sshd -t -f /etc/ssh/sshd_config``

	An incorrect configuration will prevent the sshd service from starting, which might mean losing access to reach the server.

I noticed when the ssh session is disconnected for any reason, ``pulseaudio`` would disconnect.
This is due to PAM handling sessions. When running ``dmesg``, I noticed ``elogind`` would say removed user session.

According to this `article <https://devicetests.com/ssh-usepam-security-session-status>`_ 
disabling PAM increases security, but reduces certain functionality in terms of session handling. 
*Do so at your own risk!*

Reference:
`<https://forums.gentoo.org/viewtopic-t-1090186-start-0.html>`_

After making changes to the sshd_config, restart the sshd service for changes to take into effect.

.. tab:: SystemD

    .. code-block:: sh

		sudo systemctl restart sshd.service

.. tab:: Runit

    .. code-block:: sh

		sudo sv restart sshd

----

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

.. note::
	The ``ConnectedMonitor`` tricks the GPU into thinking a monitor is connected, even if there is none actually connected! 
	This allows a virtual display to be created that is accelerated with your GPU! The ``ModeValidation`` option disables valid resolution checks,
	so you can choose any resolution on the host!


UINPUT Workaround
++++++++++++++++++

.. admonition:: Why is this necessary?
	:class: important

	After I setup the :ref:`udev rule <about/usage:linux>` to get access to ``/dev/uinput``,
	I noticed when I sshed into the host without physical login, the ACL permissions on ``/dev/uinput`` were not changed.
	So I asked `reddit <https://www.reddit.com/r/linux_gaming/comments/14htuzv/does_sshing_into_host_trigger_udev_rule_on_the/>`_.
	I discovered that SSH sessions are not the same as a physical login.
	I suppose it's not possible for SSH to trigger a udev rule.

.. caution:: Do so at your own risk! It is more secure to give sudo and no password prompt to a single script, than a generic executable like chown.

**Script**

Two scripts will need to be written to get this setup

#. Script to update permissions on ``/dev/uinput``. Since we aren't logged into the host, the udev rule doesn't apply.
#. Script to start up X server and sunshine

**Setup Script**

We will manually change the permissions of ``/dev/uinput`` using ``chown``. You need to use ``sudo`` to make this change, so add/update the entry in ``/etc/sudoers.d/<user>``

.. code-block::

	<user> ALL=(ALL:ALL) ALL, NOPASSWD: /home/<user>/scripts/sunshine-setup.sh

These changes allow the script to use sudo without being prompted with a password.


Putting Everything Together
+++++++++++++++++++++++++++


**sunshine-setup.sh**

.. code-block:: sh

	#!/bin/bash
	chown <user>:<user> /dev/uinput

	# Optional
	# blocks wifi, so ethernet is used
	# use rfkill list to get the id of the Wiresless LAN
	# rfkill block <wireless_lan_index>

**Sunshine Startup Script**

.. code-block:: sh

	#!/bin/bash

	export DISPLAY=:0

	# Check existing X server
	ps -e | grep X >/dev/null
	[[ ${?} -ne 0 ]] && {
	  echo "Starting X server"
	  startx &>/dev/null &
	  [[ ${?} -eq 0 ]] && {
	    echo "X server started successfully"
	  } || echo "X server failed to start"
	} || echo "X server already running"

	# Check if sunshine is already running
	ps -e | grep -e .*sunshine$ >/dev/null
	[[ ${?} -ne 0 ]] && {
	  sudo ~/scripts/update-udev.sh
	  sleep 1
	  echo "Starting Sunshine!"
	  sunshine > /dev/null &
	  [[ ${?} -eq 0 ]] && {
	    echo "Sunshine started successfully"
	  } || echo "Sunshine failed to start"
	} || echo "Sunshine is already running"

SSH Client Setup
^^^^^^^^^^^^^^^^

We will be setting up:

#. `SSH key generation <ssh key authentication setup_>`_
#. `Script to SSH into host to execute sunshine start up script <ssh client script_>`_

SSH Key Authentication Setup
+++++++++++++++++++++++++++++

#. Setup your SSH keys with ``ssh-keygen`` and use ``ssh-copy-id`` to authorize remote login to your host. Run ``ssh <user>@<ip_address>`` to login to your host. SSH keys automate login so you don't need to input your password!
#. Optionally setup a ``~/.ssh/config`` file to simplify the ``ssh`` command
   
   .. code-block::

		Host <some_alias>
		    Hostname <ip_address>
		    User <username>
		    IdentityFile ~/.ssh/<your_private_key>

   Now you can use ``ssh <some_alias>``.  
   ``ssh <some_alias> <commands/script>`` will execute the command or script on the remote host.

SSH Client Script
+++++++++++++++++
This bash script will automate the startup of the X server and Sunshine on the host.
This can be run on linux / macOS system.
On Windows, this can be run inside a ``git-bash``

For Android/IOS you can install linux emulators. E.g. ``Userland`` for Android and ``ISH`` for IOS 

.. code-block:: sh

	#!/bin/bash

	ssh_args="eric@192.168.1.3"

	check_ssh(){
	  result=1
	  while [[ $result -ne 0 ]]
	  do
	    echo "checking host..."
	    ssh $ssh_args "exit 0" 2>/dev/null
	    result=$?
	    [[ $result -ne 0 ]] && {
	  	  echo "Failed to ssh to $ssh_args, with exit code $result"
	    }
	    sleep 3
	  done
	  echo "Host is ready for streaming!"
	}

	start_stream(){
	  echo "Starting sunshine server on host..."
	  echo "Start moonlight on your client of choice"
	  ssh -f $ssh_args "~/scripts/sunshine.sh &"
	}

	check_ssh
	start_stream
	exit_code=${?}

	sleep 3
	exit ${exit_code}


