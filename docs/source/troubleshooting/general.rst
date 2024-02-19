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
