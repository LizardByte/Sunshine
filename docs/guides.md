# Guides

@admonition{Community | This collection of guides is written by the community!
Feel free to contribute your own tips and trips by making a PR.}


## Linux

### Discord call cancellation

| Author     | [RickAndTired](https://github.com/RickAndTired) |
|------------|-------------------------------------------------|
| Difficulty | Easy                                            |

* Set your normal *Sound Output* volume to 100%

  ![](images/discord_calls_01.png)

* Start Sunshine

* Set *Sound Output* to *sink-sunshine-stereo* (if it isn't automatic)

  ![](images/discord_calls_02.png)

* In Discord, right click *Deafen* and select your normal *Output Device*.
  This is also where you will need to adjust output volume for Discord calls

  ![](images/discord_calls_03.png)

* Open *qpwgraph*

  ![](images/discord_calls_04.png)

* Connect `sunshine [sunshine-record]` to your normal *Output Device*
  * Drag `monitor_FL` to `playback_FL`
  * Drag `monitor_FR` to `playback_FR`

  ![](images/discord_calls_05.png)

### Remote SSH Headless Setup

| Author     | [Eric Dong](https://github.com/e-dong) |
|------------|----------------------------------------|
| Difficulty | Intermediate                           |

This is a guide to setup remote SSH into host to startup X server and Sunshine without physical login and dummy plug.
The virtual display is accelerated by the NVidia GPU using the TwinView configuration.

@attention{This guide is specific for Xorg and NVidia GPUs. I start the X server using the `startx` command.
I also only tested this on an Artix runit init system on LAN.
I didn't have to do anything special with pulseaudio (pipewire untested).

Keep your monitors plugged in until the [Checkpoint](#checkpoint) step.}

@tip{Prior to editing any system configurations, you should make a copy of the original file.
This will allow you to use it for reference or revert your changes easily.}

#### The Big Picture
Once you are done, you will need to perform these 3 steps:

1. Turn on the host machine
2. Start Sunshine on remote host with a script that:

   * Edits permissions of `/dev/uinput` (added sudo config to execute script with no password prompt)
   * Starts X server with `startx` on virtual display
   * Starts Sunshine

3. Startup Moonlight on the client of interest and connect to host

@hint{As an alternative to SSH...

**Step 2** can be replaced with autologin and starting Sunshine as a service or putting
`sunshine &` in your `.xinitrc` file if you start your X server with `startx`.
In this case, the workaround for `/dev/uinput` permissions is not needed because the udev rule would be triggered
for "physical" login. See [Linux Setup](md_docs_2getting__started.html#linux). I personally think autologin compromises
the security of the PC, so I went with the remote SSH route. I use the PC more than for gaming, so I don't need a
virtual display everytime I turn on the PC (E.g running updates, config changes, file/media server).}

First we will [setup the host](#host-setup) and then the [SSH Client](#ssh-client-setup)
(Which may not be the same as the machine running the moonlight client).

#### Host Setup
We will be setting up:

1. [Static IP Setup](#static-ip-setup)
2. [SSH Server Setup](#ssh-server-setup)
3. [Virtual Display Setup](#virtual-display-setup)
4. [Uinput Permissions Workaround](#uinput-permissions-workaround)
5. [Stream Launcher Script](#stream-launcher-script)

#### Static IP Setup
Setup static IP Address for host. For LAN connections you can use DHCP reservation within your assigned range.
e.g. 192.168.x.x. This will allow you to ssh to the host consistently, so the assigned IP address does
not change. It is preferred to set this through your router config.

#### SSH Server Setup
@note{Most distros have OpenSSH already installed. If it is not present, install OpenSSH using your package manager.}

@tabs{
  @tab{Debian based | ```bash
    sudo apt update
    sudo apt install openssh-server
    ```}
  @tab{Arch based | ```bash
    sudo pacman -S openssh
    # Install  openssh-<other_init> if you are not using SystemD
    # e.g. sudo pacman -S openssh-runit
    ```}
  @tab{Alpine based | ```bash
    sudo apk update
    sudo apk add openssh
    ```}
  @tab{Fedora based (dnf) | ```bash
    sudo dnf install openssh-server
    ```}
  @tab{Fedora based (yum) | ```bash
    sudo yum install openssh-server
    ```}
}

Next make sure the OpenSSH daemon is enabled to run when the system starts.

@tabs{
  @tab{SystemD | ```bash
    sudo systemctl enable sshd.service
    sudo systemctl start sshd.service  # Starts the service now
    sudo systemctl status sshd.service  # See if the service is running
    ```}
  @tab{Runit | ```bash
    sudo ln -s /etc/runit/sv/sshd /run/runit/service  # Enables the OpenSSH daemon to run when system starts
    sudo sv start sshd  # Starts the service now
    sudo sv status sshd  # See if the service is running
    ```}
  @tab{OpenRC | ```bash
    rc-update add sshd  # Enables service
    rc-status  # List services to verify sshd is enabled
    rc-service sshd start  # Starts the service now
    ```}
}

**Disabling PAM in sshd**

I noticed when the ssh session is disconnected for any reason, `pulseaudio` would disconnect.
This is due to PAM handling sessions. When running `dmesg`, I noticed `elogind` would say removed user session.
In this [Gentoo Forums post](https://forums.gentoo.org/viewtopic-t-1090186-start-0.html),
someone had a similar issue. Starting the X server in the background and exiting out of the console would cause your
session to be removed.

@caution{According to this [article](https://devicetests.com/ssh-usepam-security-session-status)
disabling PAM increases security, but reduces certain functionality in terms of session handling.
*Do so at your own risk!*}

Edit the ``sshd_config`` file with the following to disable PAM.

```txt
usePAM no
```

After making changes to the `sshd_config`, restart the sshd service for changes to take effect.

@tip{Run the command to check the ssh configuration prior to restarting the sshd service.
```bash
sudo sshd -t -f /etc/ssh/sshd_config
```

An incorrect configuration will prevent the sshd service from starting, which might mean
losing SSH access to the server.}

@tabs{
  @tab{SystemD | ```bash
    sudo systemctl restart sshd.service
    ```}
  @tab{Runit | ```bash
    sudo sv restart sshd
    ```}
  @tab{OpenRC | ```bash
    sudo rc-service sshd restart
    ```}
}

#### Virtual Display Setup
As an alternative to a dummy dongle, you can use this config to create a virtual display.

@important{This is only available for NVidia GPUs using Xorg.}

@hint{Use ``xrandr`` to see name of your active display output. Usually it starts with ``DP`` or ``HDMI``. For me, it is ``DP-0``.
Put this name for the ``ConnectedMonitor`` option under the ``Device`` section.

```bash
xrandr | grep " connected" | awk '{ print $1 }'
```
}

```xorg
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
```

@note{The `ConnectedMonitor` tricks the GPU into thinking a monitor is connected,
even if there is none actually connected! This allows a virtual display to be created that is accelerated with
your GPU! The `ModeValidation` option disables valid resolution checks, so you can choose any
resolution on the host!

**References**

* [issue comment on virtual-display-linux](https://github.com/dianariyanto/virtual-display-linux/issues/9#issuecomment-786389065)
* [Nvidia Documentation on Configuring TwinView](https://download.nvidia.com/XFree86/Linux-x86/270.29/README/configtwinview.html)
* [Arch Wiki Nvidia#TwinView](https://wiki.archlinux.org/title/NVIDIA#TwinView)
* [Unix Stack Exchange - How to add virtual display monitor with Nvidia proprietary driver](https://unix.stackexchange.com/questions/559918/how-to-add-virtual-monitor-with-nvidia-proprietary-driver)
}

#### Uinput Permissions Workaround

##### Steps
We can use `chown` to change the permissions from a script. Since this requires `sudo`,
we will need to update the sudo configuration to execute this without being prompted for a password.

1. Create a `sunshine-setup.sh` script to update permissions on `/dev/uinput`. Since we aren't logged into the host,
   the udev rule doesn't apply.
2. Update user sudo configuration `/etc/sudoers.d/<user>` to allow the `sunshine-setup.sh`
   script to be executed with `sudo`.

@note{After I setup the :ref:`udev rule <about/setup:install>` to get access to `/dev/uinput`, I noticed when I sshed
into the host without physical login, the ACL permissions on `/dev/uinput` were not changed. So I asked
[reddit](https://www.reddit.com/r/linux_gaming/comments/14htuzv/does_sshing_into_host_trigger_udev_rule_on_the).
I discovered that SSH sessions are not the same as a physical login.
I suppose it's not possible for SSH to trigger a udev rule or create a physical login session.}

##### Setup Script
This script will take care of any preconditions prior to starting up Sunshine.

Run the following to create a script named something like `sunshine-setup.sh`:

```bash
echo "chown $(id -un):$(id -gn) /dev/uinput" > sunshine-setup.sh && \
  chmod +x sunshine-setup.sh
```

(**Optional**) To Ensure ethernet is being used for streaming, you can block Wi-Fi with `rfkill`.

Run this command to append the rfkill block command to the script:

```bash
echo "rfkill block $(rfkill list | grep "Wireless LAN" \
  | sed 's/^\([[:digit:]]\).*/\1/')" >> sunshine-setup.sh
```

##### Sudo Configuration
We will manually change the permissions of `/dev/uinput` using `chown`.
You need to use `sudo` to make this change, so add/update the entry in `/etc/sudoers.d/${USER}`.

@danger{Do so at your own risk! It is more secure to give sudo and no password prompt to a single script,
than a generic executable like chown.}

@warning{Be very careful of messing this config up. If you make a typo, *YOU LOSE THE ABILITY TO USE SUDO*.
Fortunately, your system is not borked, you will need to login as root to fix the config.
You may want to setup a backup user / SSH into the host as root to fix the config if this happens.
Otherwise, you will need to plug your machine back into a monitor and login as root to fix this.
To enable root login over SSH edit your SSHD config, and add `PermitRootLogin yes`, and restart the SSH server.}

1. First make a backup of your `/etc/sudoers.d/${USER}` file.

   ```bash
   sudo cp /etc/sudoers.d/${USER} /etc/sudoers.d/${USER}.backup
   ```

2. `cd` to the parent dir of the `sunshine-setup.sh` script.
3. Execute the following to update your sudoer config file.

   ```bash
   echo "${USER} ALL=(ALL:ALL) ALL, NOPASSWD: $(pwd)/sunshine-setup.sh" \
     | sudo tee /etc/sudoers.d/${USER}
   ```

These changes allow the script to use sudo without being prompted with a password.

e.g. `sudo $(pwd)/sunshine-setup.sh`

#### Stream Launcher Script
This is the main entrypoint script that will run the `sunshine-setup.sh` script, start up X server, and Sunshine.
The client will call this script that runs on the host via ssh.


##### Sunshine Startup Script
This guide will refer to this script as `~/scripts/sunshine.sh`.
The setup script will be referred as `~/scripts/sunshine-setup.sh`.

```bash
#!/bin/bash
set -e

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
```

#### SSH Client Setup
We will be setting up:

1. [SSH Key Authentication Setup](#ssh-key-authentication-setup)
2. [SSH Client Script (Optional)](#ssh-client-script-optional)

##### SSH Key Authentication Setup
1. Setup your SSH keys with `ssh-keygen` and use `ssh-copy-id` to authorize remote login to your host.
   Run `ssh <user>@<ip_address>` to login to your host.
   SSH keys automate login so you don't need to input your password!
2. Optionally setup a `~/.ssh/config` file to simplify the `ssh` command

   ```txt
   Host <some_alias>
       Hostname <ip_address>
       User <username>
       IdentityFile ~/.ssh/<your_private_key>
   ```

   Now you can use `ssh <some_alias>`.
   `ssh <some_alias> <commands/script>` will execute the command or script on the remote host.

##### Checkpoint
As a sanity check, let's make sure your setup is working so far!

###### Test Steps
With your monitor still plugged into your Sunshine host PC:

1. `ssh <alias>`
2. `~/scripts/sunshine.sh`
3. `nvidia-smi`

   You should see the Sunshine and Xorg processing running:

   ```bash
   nvidia-smi
   ```

   *Output:*
   ```txt
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
   ```

4. Check `/dev/uinput` permissions

   ```bash
   ls -l /dev/uinput
   ```

   *Output:*

   ```console
   crw------- 1 <user> <primary_group> 10, 223 Aug 29 17:31 /dev/uinput
   ```

5. Connect to Sunshine host from a moonlight client

Now kill X and Sunshine by running `pkill X` on the host, unplug your monitors from your GPU, and repeat steps 1 - 5.
You should get the same result.
With this setup you don't need to modify the Xorg config regardless if monitors are plugged in or not.

```bash
pkill X
```

##### SSH Client Script (Optional)
At this point you have a working setup! For convenience, I created this bash script to automate the
startup of the X server and Sunshine on the host.
This can be run on Unix systems, or on Windows using the `git-bash` or any bash shell.

For Android/iOS you can install Linux emulators, e.g. `Userland` for Android and `ISH` for iOS.
The neat part is that you can execute one script to launch Sunshine from your phone or tablet!

```bash
#!/bin/bash
set -e

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
```

#### Next Steps
Congratulations, you can now stream your desktop headless! When trying this the first time,
keep your monitors close by incase something isn't working right.

@seealso{Now that you have a virtual display, you may want to automate changing the resolution
and refresh rate prior to connecting to an app. See
[Changing Resolution and Refresh Rate](md_docs_2app__examples#changing-resolution-and-refresh-rate)
for more information.}

### Start sunshine on boot for Plasma with Wayland without autologin
| Author     | [MidwesternRodent](https://github.com/midwesternrodent/) |
|------------|----------------------------------------------------------|
| Difficulty | Advanced                                                 |

This is a guide for how to start sunshine automatically at boot without enabling autologin for the desktop. I have tested this guide on Debian 12 with KDE Plasma and Wayland, and running the .deb version of Sunshine version 0.23.1. Special thanks to [this user on unix stackexchange](https://unix.stackexchange.com/a/669476) for giving a good run down of how to enable the ssh connection on boot.

#### Expected Behavior after following this guide
Any time you boot your computer (wake on lan is highly recommended for this setup), sunshine will start automatically and you can remote into your machine from moonlight without performing any additional steps.

#### Caveats
1. This does not work with a multi-monitor setup at this time. If you have multiple monitors you will be able to phsyically log into the machine, but logging in to Wayland via Moonlight will result in all of your physical displys turning off and moonlight displaying a black screen. Restarting SDDM brings the login screen back, but logging in physically or remotely will result in the same behavior after that point until you reboot the machine.
2. You cannot modify the display sizes via a prep command in sunshine if you are using this setup. Doing so will result in a failure to launch the application from Moonlight. You can still use kscreen-doctor or modify the displays manually via the system settigns after you've logged into wayland. This is because we are botting to wayland and plasma is not initialized yet, so kscreen-doctor cannot access the display to modify them and will fail outright until you log in.

#### Prerequisites and scope
1. You are running Debian 12 with KDE Plasma and Wayland (the standard setup for KDE Plasma on Debian 12)
2. You have followed [Eric Dong's Guide for a Remote SSH Headless Setup](https://docs.lizardbyte.dev/projects/sunshine/en/latest/about/guides/linux/headless_ssh.html) and can already manually start a session by SSHing into your machine and running the sunshine.sh script. For our purposes, you do not need to the headless portion, and this guide does not do so as I'm using an AMD GPU.
3. If not already obvious from prerequisite 2, you must have an SSH server already running on the machine you intend to remote into. We will be SSHing into the localhost as part of this guide.
4. This doc is only about enabling you to turn an already functioning manual-start sunshine instalation, into an auto-start sunshine client. If sunshine is not working for you, fix it first, and then come back to this guide.
5. You have disabled any auto-start of sunshine on login through systemd or the plasma settings.

#### Allow the sunshine host to SSH to itself

Skip this step if you already have an SSH identity file for the sunshine host. Sign into the user you plan to use sunshine with and run the following command to generate an SSH identity file.
```
ssh-keygen
```

Copy the contents of your newly generated SSH public key to your authorized keys file.

```
cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys
```

Ensure that the following lines exist in /etc/ssh/sshd_config and are uncommented.

```
PubkeyAuthentication yes
PasswordAuthentication no
```

Reboot the computer, log in, and ensure you can run the following command (replace username with your user's name) to confirm you did everything correctly. It should not prompt you for a password. If it works you will start a new shell for your user in the ssh session.
```
ssh username@localhost
```

#### Create the auto start script
Create the following file and copy the below contents into it. Replace username with your username.

/usr/local/bin/autossh-sunshine-start

```
#!/bin/bash
# establish an SSH connection, and run the script described in the remote headless documentation for sunshine
ssh -i /home/username/.ssh/id_rsa username@localhost "/home/username/scripts/sunshine.sh"
```

Once the script is created be sure to run the following to enable execution

```
sudo chmod +x /usr/local/bin/autossh-sunshine-start
```

#### Create the autostart service
Create the following file with the below contents at /etc/systemd/system/autossh-sunshine-start.service.
```
[Unit]
Description=Start and SSH tunnel on boot and run the sunshine.sh script.
# obviously, this can't run without sshd running so wait for that to start.
Requires=sshd.service  
After=sshd.service  

[Service]
# Sometimes it starts before the wayland screens have really initialized and moonlight fails to connect.
# waiting a few seconds seems to fix this. 
ExecStartPre=/bin/sleep 5  
ExecStart=/usr/local/bin/autossh-sunshine-start  
Restart=on-failure  
RestartSec=5s  
  
[Install]  
WantedBy=multi-user.target
```

If you have still have it enabled to start on boot, stop and disable the sunshine service by running the following:
```
sudo systemctl stop sunshine
sudo systemctl disable susnhine
```

And finally, enable and start your new service

```
sudo systemctl enable autossh-sunshine-start
sudo systemctl start autossh-sunshine-start
```

Sunshine should now start automatically anytime you boot, and await your login on the wayland screen.


## macOS
@todo{It's looking lonely here.}


## Windows

| Author     | [BeeLeDev](https://github.com/BeeLeDev) |
|------------|-----------------------------------------|
| Difficulty | Intermediate                            |

### Discord call cancellation
Cancel Discord call audio with Voicemeeter (Standard)

#### Voicemeeter Configuration
1. Click "Hardware Out"
2. Set the physical device you receive audio to as your Hardware Out with MME
3. Turn on BUS A for the Virtual Input

#### Windows Configuration
1. Open the sound settings
2. Set your default Playback as Voicemeeter Input

@tip{Run audio in the background to find the device that your Virtual Input is using
(Voicemeeter In #), you will see the bar to the right of the device have green bars
going up and down. This device will be referred to as Voicemeeter Input.}

#### Discord Configuration
1. Open the settings
2. Go to Voice & Video
3. Set your Output Device as the physical device you receive audio to

@tip{It is usually the same device you set for Hardware Out in Voicemeeter.}

#### Sunshine Configuration
1. Go to Configuration
2. Go to the Audio/Video tab
3. Set Virtual Sink as Voicemeeter Input

@note{This should be the device you set as default previously in Playback.}

<div class="section_buttons">

| Previous                        |                                        Next |
|:--------------------------------|--------------------------------------------:|
| [App Examples](app_examples.md) | [Performance Tuning](performance_tuning.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
