Remote SSH Headless Setup
=========================

.. csv-table:: Remote SSH Headless Setup
   :header-rows: 0
   :stub-columns: 1

   Author, `Eric Dong <https://github.com/e-dong>`__
   Difficulty, Intermediate

This is a guide to setup remote SSH into host to startup X server and sunshine without physical login and dummy plug.
The virtual display is accelerated by the NVidia GPU using the TwinView configuration.

.. attention::
    This guide is specific for Xorg and NVidia GPUs. I start the X server using the ``startx`` command.
    I also only tested this on an Artix runit init system on LAN.
    I didn't have to do anything special with pulseaudio (pipewire untested).

    Keep your monitors plugged in until the `Checkpoint`_ step

.. tip::
   Prior to editing any system configurations, you should make a copy of the original file.
   This will allow you to use it for reference or revert your changes easily.

The Big Picture
---------------
Once you are done, you will need to perform these 3 steps:

#. Turn on the host machine
#. Start sunshine on remote host with a script that:

   - Edits permissions of ``/dev/uinput`` (added sudo config to execute script with no password prompt)
   - Starts X server with ``startx`` on virtual display
   - Starts ``Sunshine``

#. Startup Moonlight on the client of interest and connect to host

.. hint::

   As an alternative to SSH...

   **Step 2** can be replaced with autologin and starting sunshine as a service or putting
   ``sunshine &`` in your ``.xinitrc`` file if you start your X server with ``startx``.
   In this case, the workaround for ``/dev/uinput`` permissions is not needed because the udev rule would be triggered
   for "physical" login. See :ref:`Linux Setup <about/setup:install>`. I personally think autologin compromises the
   security of the PC, so I went with the remote SSH route. I use the PC more than for gaming, so I don't need a
   virtual display everytime I turn on the PC (E.g running updates, config changes, file/media server).

First we will setup the host and then the SSH Client (Which may not be the same as the machine running the
moonlight client)

Host Setup
----------

We will be setting up:

#. `Static IP Setup`_
#. `SSH Server Setup`_
#. `Virtual Display Setup`_
#. `Uinput Permissions Workaround`_
#. `Stream Launcher Script`_


Static IP Setup
^^^^^^^^^^^^^^^
Setup static IP Address for host. For LAN connections you can use DHCP reservation within your assigned range.
e.g. 192.168.x.x. This will allow you to ssh to the host consistently, so the assigned IP address does
not change. It is preferred to set this through your router config.


SSH Server Setup
^^^^^^^^^^^^^^^^

.. note::
   Most distros have OpenSSH already installed. If it is not present, install OpenSSH using your package manager.

.. tab:: Debian/Ubuntu

   .. code-block:: bash

      sudo apt update
      sudo apt install openssh-server

.. tab:: Arch/Artix

   .. code-block:: bash

      sudo pacman -S openssh
      # Install  openssh-<other_init> if you are not using SystemD
      # e.g. sudo pacman -S openssh-runit

.. tab:: Alpine

   .. code-block:: bash

        sudo apk update
        sudo apk add openssh

.. tab:: CentOS/RHEL/Fedora

   **CentOS/RHEL 7**
      .. code-block:: bash

         sudo yum install openssh-server

   **CentOS/Fedora/RHEL 8**
      .. code-block:: bash

         sudo dnf install openssh-server

Next make sure the OpenSSH daemon is enabled to run when the system starts.

.. tab:: SystemD

    .. code-block:: bash

      sudo systemctl enable sshd.service
      sudo systemctl start sshd.service  # Starts the service now
      sudo systemctl status sshd.service  # See if the service is running

.. tab:: Runit

   .. code-block:: bash

      sudo ln -s /etc/runit/sv/sshd /run/runit/service  # Enables the OpenSSH daemon to run when system starts
      sudo sv start sshd  # Starts the service now
      sudo sv status sshd  # See if the service is running

.. tab:: OpenRC

    .. code-block:: bash

        rc-update add sshd  # Enables service
        rc-status  # List services to verify sshd is enabled
        rc-service sshd start  # Starts the service now

**Disabling PAM in sshd**

I noticed when the ssh session is disconnected for any reason, ``pulseaudio`` would disconnect.
This is due to PAM handling sessions. When running ``dmesg``, I noticed ``elogind`` would say removed user session.
In this `Gentoo Forums post <https://forums.gentoo.org/viewtopic-t-1090186-start-0.html>`__,
someone had a similar issue. Starting the X server in the background and exiting out of the console would cause your
session to be removed.

.. caution::
   According to this `article <https://devicetests.com/ssh-usepam-security-session-status>`__
   disabling PAM increases security, but reduces certain functionality in terms of session handling.
   *Do so at your own risk!*

Edit the ``sshd_config`` file with the following to disable PAM.

.. code-block:: text

   usePAM no

After making changes to the ``sshd_config``, restart the sshd service for changes to take effect.

.. tip::
   Run the command to check the ssh configuration prior to restarting the sshd service.

   .. code-block:: bash

      sudo sshd -t -f /etc/ssh/sshd_config

   An incorrect configuration will prevent the sshd service from starting, which might mean
   losing SSH access to the server.

.. tab:: SystemD

    .. code-block:: bash

      sudo systemctl restart sshd.service

.. tab:: Runit

    .. code-block:: bash

      sudo sv restart sshd

.. tab:: OpenRC

    .. code-block:: bash

      sudo rc-service sshd restart


Virtual Display Setup
^^^^^^^^^^^^^^^^^^^^^

As an alternative to a dummy dongle, you can use this config to create a virtual display.

.. important::
   This is only available for NVidia GPUs using Xorg.

.. hint::
   Use ``xrandr`` to see name of your active display output. Usually it starts with ``DP`` or ``HDMI``. For me, it is ``DP-0``.
   Put this name for the ``ConnectedMonitor`` option under the ``Device`` section.

   .. code-block:: bash

      xrandr | grep " connected" | awk '{ print $1 }'


.. code-block:: xorg.conf

   Section "ServerLayout"
       Identifier "TwinLayout"
       Screen 0 "metaScreen" 0 0
   EndSection

   Section "Monitor"
       Identifier "Monitor0"
       Option "Enable" "true"
   EndSection

   Section "Device"
       Identifier "Card0"
       Driver "nvidia"
       VendorName "NVIDIA Corporation"
       Option "MetaModes" "1920x1080"
       Option "ConnectedMonitor" "DP-0"
       Option "ModeValidation" "NoDFPNativeResolutionCheck,NoVirtualSizeCheck,NoMaxPClkCheck,NoHorizSyncCheck,NoVertRefreshCheck,NoWidthAlignmentCheck"
   EndSection

   Section "Screen"
       Identifier "metaScreen"
       Device "Card0"
       Monitor "Monitor0"
       DefaultDepth 24
       Option "TwinView" "True"
       SubSection "Display"
           Modes "1920x1080"
       EndSubSection
   EndSection

.. note::
   The ``ConnectedMonitor`` tricks the GPU into thinking a monitor is connected,
   even if there is none actually connected! This allows a virtual display to be created that is accelerated with
   your GPU! The ``ModeValidation`` option disables valid resolution checks, so you can choose any
   resolution on the host!

   **References**

   - `issue comment on virtual-display-linux
     <https://github.com/dianariyanto/virtual-display-linux/issues/9#issuecomment-786389065>`__
   - `Nvidia Documentation on Configuring TwinView
     <https://download.nvidia.com/XFree86/Linux-x86/270.29/README/configtwinview.html>`__
   - `Arch Wiki Nvidia#TwinView <https://wiki.archlinux.org/title/NVIDIA#TwinView>`__
   - `Unix Stack Exchange - How to add virtual display monitor with Nvidia proprietary driver
     <https://unix.stackexchange.com/questions/559918/how-to-add-virtual-monitor-with-nvidia-proprietary-driver>`__


Uinput Permissions Workaround
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Steps**

We can use ``chown`` to change the permissions from a script. Since this requires ``sudo``,
we will need to update the sudo configuration to execute this without being prompted for a password.

#. Create a ``sunshine-setup.sh`` script to update permissions on ``/dev/uinput``. Since we aren't logged into the host,
   the udev rule doesn't apply.
#. Update user sudo configuration ``/etc/sudoers.d/<user>`` to allow the ``sunshine-setup.sh``
   script to be executed with ``sudo``.

.. note::
   After I setup the :ref:`udev rule <about/setup:install>` to get access to ``/dev/uinput``,
   I noticed when I sshed into the host without physical login, the ACL permissions on ``/dev/uinput`` were not changed.
   So I asked `reddit
   <https://www.reddit.com/r/linux_gaming/comments/14htuzv/does_sshing_into_host_trigger_udev_rule_on_the/>`__.
   I discovered that SSH sessions are not the same as a physical login.
   I suppose it's not possible for SSH to trigger a udev rule or create a physical login session.

**Setup Script**

This script will take care of any preconditions prior to starting up sunshine.

Run the following to create a script named something like ``sunshine-setup.sh``:
   .. code-block:: bash

      echo "chown $(id -un):$(id -gn) /dev/uinput" > sunshine-setup.sh &&\
        chmod +x sunshine-setup.sh

(**Optional**) To Ensure ethernet is being used for streaming,
you can block WiFi with ``rfkill``.

Run this command to append the rfkill block command to the script:
   .. code-block:: bash

      echo "rfkill block $(rfkill list | grep "Wireless LAN" \
        | sed 's/^\([[:digit:]]\).*/\1/')" >> sunshine-setup.sh

**Sudo Configuration**

We will manually change the permissions of ``/dev/uinput`` using ``chown``.
You need to use ``sudo`` to make this change, so add/update the entry in ``/etc/sudoers.d/${USER}``

.. danger::
   Do so at your own risk! It is more secure to give sudo and no password prompt to a single script,
   than a generic executable like chown.

.. warning::
   Be very careful of messing this config up. If you make a typo, *YOU LOSE THE ABILITY TO USE SUDO*.
   Fortunately, your system is not borked, you will need to login as root to fix the config.
   You may want to setup a backup user / SSH into the host as root to fix the config if this happens.
   Otherwise you will need to plug your machine back into a monitor and login as root to fix this.
   To enable root login over SSH edit your SSHD config, and add ``PermitRootLogin yes``, and restart the SSH server.

#. First make a backup of your ``/etc/sudoers.d/${USER}`` file.

   .. code-block:: bash

      sudo cp /etc/sudoers.d/${USER} /etc/sudoers.d/${USER}.backup

#. ``cd`` to the parent dir of the ``sunshine-setup.sh`` script.
#. Execute the following to update your sudoer config file.

   .. code-block:: bash

      echo "${USER} ALL=(ALL:ALL) ALL, NOPASSWD: $(pwd)/sunshine-setup.sh" \
        | sudo tee /etc/sudoers.d/${USER}

These changes allow the script to use sudo without being prompted with a password.

e.g. ``sudo $(pwd)/sunshine-setup.sh``


Stream Launcher Script
^^^^^^^^^^^^^^^^^^^^^^

This is the main entrypoint script that will run the ``sunshine-setup.sh`` script, start up X server, and Sunshine.
The client will call this script that runs on the host via ssh.


**Sunshine Startup Script**

This guide will refer to this script as ``~/scripts/sunshine.sh``.
The setup script will be referred as ``~/scripts/sunshine-setup.sh``

.. code-block:: bash

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
     sudo ~/scripts/sunshine-setup.sh
     echo "Starting Sunshine!"
     sunshine > /dev/null &
     [[ ${?} -eq 0 ]] && {
       echo "Sunshine started successfully"
     } || echo "Sunshine failed to start"
    } || echo "Sunshine is already running"

    # Add any other Programs that you want to startup automatically
    # e.g.
    # steam &> /dev/null &
    # firefox &> /dev/null &
    # kdeconnect-app &> /dev/null &

----

SSH Client Setup
----------------

We will be setting up:

#. `SSH Key Authentication Setup`_
#. `SSH Client Script (Optional)`_

SSH Key Authentication Setup
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

#. Setup your SSH keys with ``ssh-keygen`` and use ``ssh-copy-id`` to authorize remote login to your host.
   Run ``ssh <user>@<ip_address>`` to login to your host.
   SSH keys automate login so you don't need to input your password!
#. Optionally setup a ``~/.ssh/config`` file to simplify the ``ssh`` command

   .. code-block:: text

      Host <some_alias>
          Hostname <ip_address>
          User <username>
          IdentityFile ~/.ssh/<your_private_key>

   Now you can use ``ssh <some_alias>``.
   ``ssh <some_alias> <commands/script>`` will execute the command or script on the remote host.

Checkpoint
^^^^^^^^^^

As a sanity check, let's make sure your setup is working so far!

**Test Steps**

With your monitor still plugged into your Sunshine host PC:

#. ``ssh <alias>``
#. ``~/scripts/sunshine.sh``
#. ``nvidia-smi``

   You should see the sunshine and Xorg processing running:

   .. code-block:: bash

      nvidia-smi

   *Output:*

   .. code-block:: console

       +---------------------------------------------------------------------------------------+
       | NVIDIA-SMI 535.104.05             Driver Version: 535.104.05   CUDA Version: 12.2     |
       |-----------------------------------------+----------------------+----------------------+
       | GPU  Name                 Persistence-M | Bus-Id        Disp.A | Volatile Uncorr. ECC |
       | Fan  Temp   Perf          Pwr:Usage/Cap |         Memory-Usage | GPU-Util  Compute M. |
       |                                         |                      |               MIG M. |
       |=========================================+======================+======================|
       |   0  NVIDIA GeForce RTX 3070        Off | 00000000:01:00.0  On |                  N/A |
       | 30%   46C    P2              45W / 220W |    549MiB /  8192MiB |      2%      Default |
       |                                         |                      |                  N/A |
       +-----------------------------------------+----------------------+----------------------+

       +---------------------------------------------------------------------------------------+
       | Processes:                                                                            |
       |  GPU   GI   CI        PID   Type   Process name                            GPU Memory |
       |        ID   ID                                                             Usage      |
       |=======================================================================================|
       |    0   N/A  N/A      1393      G   /usr/lib/Xorg                                86MiB |
       |    0   N/A  N/A      1440    C+G   sunshine                                    293MiB |
       +---------------------------------------------------------------------------------------+

#. Check ``/dev/uinput`` permissions

   .. code-block:: bash

      ls -l /dev/uinput

   *Output:*

   .. code-block:: console

      crw------- 1 <user> <primary_group> 10, 223 Aug 29 17:31 /dev/uinput

#. Connect to Sunshine host from a moonlight client

Now kill X and sunshine by running ``pkill X`` on the host,
unplug your monitors from your GPU, and repeat steps 1 - 5.
You should get the same result.
With this setup you don't need to modify the Xorg config regardless if monitors are plugged in or not.

.. code-block:: bash

   pkill X


SSH Client Script (Optional)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

At this point you have a working setup! For convenience I created this bash script to automate the
startup of the X server and Sunshine on the host.
This can be run on Unix systems, or on Windows using the ``git-bash`` or any bash shell.

For Android/iOS you can install Linux emulators, e.g. ``Userland`` for Android and ``ISH`` for iOS.
The neat part is that you can execute one script to launch Sunshine from your phone or tablet!

.. code-block:: bash

   #!/bin/bash

   ssh_args="<user>@192.168.X.X" # Or use alias set in ~/.ssh/config

   check_ssh(){
     result=1
      # Note this checks infinitely, you could update this to have a max # of retries
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
      # -f runs ssh in the background
     ssh -f $ssh_args "~/scripts/sunshine.sh &"
   }

   check_ssh
   start_stream
   exit_code=${?}

   sleep 3
   exit ${exit_code}

Next Steps
----------

Congrats you can now stream your desktop headless! When trying this the first time,
keep your monitors close by incase something isn't working right.

If you have any feedback and any suggestions, feel free to make a post on Discord!

.. seealso::
   Now that you have a virtual display, you may want to automate changing the resolution
   and refresh rate prior to connecting to an app. See :ref:`Changing Resolution and Refresh Rate
   <about/guides/app_examples:changing resolution and refresh rate>` for more information.
