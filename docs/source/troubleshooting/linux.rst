Linux
=====

KMS Streaming fails
-------------------
If screencasting fails with KMS, you may need to run the following to force unprivileged screencasting.
   .. code-block:: bash

      sudo setcap -r $(readlink -f $(which sunshine))

Gamescope compatibility
-------------------
Some users have reported stuttering issues when streaming games running within Gamescope. 
