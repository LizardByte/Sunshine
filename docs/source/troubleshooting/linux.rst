:github_url: https://github.com/SunshineStream/Sunshine/tree/nightly/docs/source/troubleshooting/linux.rst

Linux
=====
If screencasting fails with Wayland, you may need to run the following to force screencasting with X11.

   .. code-block:: bash

      sudo setcap -r $(readlink -f $(which sunshine))
