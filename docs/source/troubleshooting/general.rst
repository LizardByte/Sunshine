General
=======

Forgotten Credentials
---------------------
If you forgot your credentials to the web UI, try this.
   .. tab:: General

      .. code-block:: bash

         sunshine --creds {new-username} {new-password}

   .. tab:: AppImage

      .. code-block:: bash

         ./sunshine.AppImage --creds {new-username} {new-password}

   .. tab:: Flatpak

      .. code-block:: bash

         flatpak run --command=sunshine dev.lizardbyte.Sunshine --creds {new-username} {new-password}


Web UI Access
-------------
Can't access the web UI?
   #. Check firewall rules.

Nvidia issues
-------------
NvFBC, NvENC, or general issues with Nvidia graphics card.
  - Consumer grade Nvidia cards are software limited to a specific number of encodes. See
    `Video Encode and Decode GPU Support Matrix <https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new>`__
    for more info.
  - You can usually bypass the restriction with a driver patch. See Keylase's
    `Linux <https://github.com/keylase/nvidia-patch>`__
    or `Windows <https://github.com/keylase/nvidia-patch/blob/master/win>`__ patches for more guidance.

Controller works on Steam but not in games
------------------------------------------
One trick might be to change Steam settings and check or uncheck the configuration to support Xbox/Playstation
controllers and leave only support for Generic controllers.

Also, if you have many controllers already directly connected to the host, it might help to disable them so that the
Sunshine provided controller (connected to the guest) is the "first" one. In Linux this can be accomplished on USB
devices by finding the device in `/sys/bus/usb/devices/` and writing `0` to the `authorized` file.

Packet loss
-----------
Albeit unlikely, some guests might work better with a lower `MTU
<https://en.wikipedia.org/wiki/Maximum_transmission_unit>`__ from the host. For example, a LG TV was found to have 30-60%
packet loss when the host had MTU set to 1500 and 1472, but 0% packet loss with a MTU of 1428 set in the network card
serving the stream (a Linux PC). It's unclear how that helped precisely so it's a last resort suggestion.
