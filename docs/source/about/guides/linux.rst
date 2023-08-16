Linux
======
Collection of Sunshine Linux host guides.

Remote SSH Headless Setup
-------------------------
Author: Eric Dong

This is a guide to setup remote SSH into host to startup X server and sunshine without physical login and dummy plug.

.. Attention:: This guide is specific for Xorg and NVidia GPUs. I also only tested this for LAN connections.

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
2. Virtual Display Acceleration via NVIDIA's TwinView X11 Config
3. Script for ``uinput`` permission workaround
4. Script to put everything together to startup X server and Sunshine

SSH Client Setup
^^^^^^^^^^^^^^^^

We will be setting up:

1. SSH key generation
2. Script to SSH into host to execute sunshine script from Host Setup in step 4.
