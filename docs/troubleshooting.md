# Troubleshooting

## General

### Forgotten Credentials
If you forgot your credentials to the web UI, try this.

@tabs{
  @tab{General | ```bash
    sunshine --creds {new-username} {new-password}
    ```
  }
  @tab{AppImage | ```bash
    ./sunshine.AppImage --creds {new-username} {new-password}
    ```
  }
  @tab{Flatpak | ```bash
    flatpak run --command=sunshine dev.lizardbyte.app.Sunshine --creds {new-username} {new-password}
    ```
  }
}

> [!TIP]
> Remember to replace `{new-username}` and `{new-password}` with your new credentials.
> Do not include the curly braces.

### Unusual Mouse Behavior
If you experience unusual mouse behavior, try attaching a physical mouse to the Sunshine host.

### Web UI Access
Can't access the web UI?

1. Check firewall rules.

### Controller works on Steam but not in games
One trick might be to change Steam settings and check or uncheck the configuration to support Xbox/PlayStation
controllers and leave only support for Generic controllers.

Also, if you have many controllers already directly connected to the host, it might help to disable them so that the
Sunshine-provided controller (connected to the guest) is the "first" one. In Linux this can be achieved on USB
devices by finding the device in `/sys/bus/usb/devices/` and writing `0` to the `authorized` file.

### Network performance test

For real-time game streaming the most important characteristic of the network
path between server and client is not pure bandwidth but rather stability and
consistency (low latency with low variance, minimal or no packet loss).

The network can be tested using the multi-platform tool [iPerf3](https://iperf.fr).

On the Sunshine host `iperf3` is started in server mode:

```bash
iperf3 -s
```

On the client device iperf3 is asked to perform a 60-second UDP test in a reverse
direction (from server to client) at a given bitrate (e.g. 50 Mbps):

```bash
iperf3 -c {HostIpAddress} -t 60 -u -R -b 50M
```

Watch the output on the client for packet loss and jitter values. Both should be
(very) low. Ideally, packet loss remains less than 5% and jitter below 1 ms.

For Android clients use
[PingMaster](https://play.google.com/store/apps/details?id=com.appplanex.pingmasternetworktools).

For iOS clients use [HE.NET Network Tools](https://apps.apple.com/us/app/he-net-network-tools/id858241710).

If you are testing a remote connection (over the internet), you will need to
forward the port 5201 (TCP and UDP) from your host.

### Packet loss (Buffer overrun)
If the host PC (running Sunshine) has a much faster connection to the network
than the slowest segment of the network path to the client device (running
Moonlight), massive packet loss can occur: Sunshine emits its stream in bursts
every 16 ms (for 60 fps), but those bursts can't be passed on fast enough to the
client and must be buffered by one of the network devices inbetween. If the
bitrate is high enough, these buffers will overflow and data will be discarded.

This can easily happen if e.g., the host has a 2.5 Gbit/s connection and the
client only 1 Gbit/s or Wi-Fi. Similarly, a 1 Gbps host may be too fast for a
client having only a 100 Mbps interface.

As a workaround the transmission speed of the host NIC can be reduced: 1 Gbps
instead of 2.5 or 100 Mbps instead of 1 Gbps. A technically more advanced
solution would be to configure traffic shaping rules at the OS level, so that
only Sunshine's traffic is slowed down.

Such a solution on Linux could look like that:

```bash
# 1) Remove existing qdisc (pfifo_fast)
sudo tc qdisc del dev <NIC> root

# 2) Add HTB root qdisc with default class 1:1
sudo tc qdisc add dev <NIC> root handle 1: htb default 1

# 3) Create class 1:1 for full 10 Gbit/s (all other traffic)
sudo tc class add dev <NIC> parent 1: classid 1:1 htb \
    rate 10000mbit ceil 10000mbit burst 32k

# 4) Create class 1:10 for Sunshine game stream at 1 Gbit/s
sudo tc class add dev <NIC> parent 1: classid 1:10 htb \
    rate 1000mbit ceil 1000mbit burst 32k

# 5) Filter UDP source port 47998 into class 1:10
sudo tc filter add dev <NIC> protocol ip parent 1: prio 1 \
    u32 match ip protocol 17 0xff \
    match ip sport 47998 0xffff flowid 1:10
```

In that way only the Sunshine traffic is limited by 1 Gbit. This is not persistent on reboots.
If you use a different port for the game stream, you need to adjust the last command.

Sunshine versions > 0.23.1 include improved networking code that should
alleviate or even solve this issue (without reducing the NIC speed).

### Packet loss (MTU)
Although unlikely, some guests might work better with a lower
[MTU](https://en.wikipedia.org/wiki/Maximum_transmission_unit) from the host.
For example, an LG TV was found to have 30–60% packet loss when the host had MTU
set to 1500 and 1472, but 0% packet loss with a MTU of 1428 set in the network card
serving the stream (a Linux PC). It's unclear how that helped precisely, so it's a last
resort suggestion.

## Linux

### Hardware Encoding fails
Due to legal concerns, Mesa has disabled hardware decoding and encoding by default.

```txt
Error: Could not open codec [h264_vaapi]: Function not implemented
```

If you see the above error in the Sunshine logs, compiling *Mesa* manually may be required. See the official Mesa3D
[Compiling and Installing](https://docs.mesa3d.org/install.html) documentation for instructions.

> [!IMPORTANT]
> You must re-enable the disabled encoders. You can do so by passing the following argument to the build
> system. You may also want to enable decoders, however, that is not required for Sunshine and is not covered here.
> ```bash
> -Dvideo-codecs=h264enc,h265enc
> ```

> [!NOTE]
> Other build options are listed in the
> [meson options](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/meson_options.txt) file.

### Input not working
After installation, the `udev` rules need to be reloaded. Our post-install script tries to do this for you
automatically, but if it fails, you may need to restart your system.

If the input is still not working, you may need to add your user to the `input` group.

```bash
sudo usermod -aG input $USER
```

### KMS Streaming fails
If screencasting fails with KMS, you may need to run the following to force unprivileged screencasting.

```bash
sudo setcap -r $(readlink -f $(which sunshine))
```

> [!NOTE]
> The above command will not work with the AppImage or Flatpak packages. Please refer to the
> [AppImage setup](md_docs_2getting__started.html#appimage) or
> [Flatpak setup](md_docs_2getting__started.html#flatpak) for more specific instructions.

### KMS streaming fails on Nvidia GPUs
If KMS screen capture results in a black screen being streamed, you may need to
set the parameter `modeset=1` for Nvidia's kernel module. This can be done by
adding the following directive to the kernel command line:

```bash
nvidia_drm.modeset=1
```

Consult your distribution's documentation for details on how to do this. (Most
often grub is used to load the kernel and set its command line.)

### AMD encoding latency issues
If you notice unexpectedly high encoding latencies (e.g., in Moonlight's
performance overlay) or strong fluctuations thereof, your system's Mesa
libraries are outdated (<24.2). This is particularly problematic at higher
resolutions (4K).

Starting with Mesa-24.2, applications can request a
[low-latency mode](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/30039)
by running them with a special
[environment variable](https://docs.mesa3d.org/envvars.html#envvar-AMD_DEBUG):
```bash
export AMD_DEBUG=lowlatencyenc
```
Sunshine sets this variable automatically, no manual
configuration is needed.

To check whether low-latency mode is being used, one can watch the VCLK and DCLK
frequencies in amdgpu_top. Without this encoder tuning both clock frequencies
will fluctuate strongly, whereas with active low-latency encoding they will stay
high as long as the encoder is used.

### Gamescope compatibility
Some users have reported stuttering issues when streaming games running within Gamescope.

## macOS

### Dynamic session lookup failed
If you get this error:

> Dynamic session lookup supported but failed: launchd did not provide a socket path, verify that
> org.freedesktop.dbus-session.plist is loaded!

Try this.
```bash
launchctl load -w /Library/LaunchAgents/org.freedesktop.dbus-session.plist
```

## Windows

### No gamepad detected
Verify that you've installed [Nefarius Virtual Gamepad](https://github.com/nefarius/ViGEmBus/releases/latest).

### Permission denied
Since Sunshine runs as a service on Windows, it may not have the same level of access that your regular user account
has. You may get permission denied errors when attempting to launch a game or application from a non-system drive.

You will need to modify the security permissions on your disk. Ensure that user/principal SYSTEM has full
permissions on the disk.

### Stuttering
If you experience stuttering using NVIDIA, try disabling `vsync:fast` in the NVIDIA Control Panel.

<div class="section_buttons">

| Previous      |                    Next |
|:--------------|------------------------:|
| [API](api.md) | [Building](building.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
