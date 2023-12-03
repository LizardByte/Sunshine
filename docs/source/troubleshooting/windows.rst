Windows
=======

No gamepad detected
-------------------
#. Verify that you've installed `Nefarius Virtual Gamepad <https://github.com/nefarius/ViGEmBus/releases/latest>`__.

Permission denied
-----------------
Since Sunshine runs as a service on Windows, it may not have the same level of access that your regular user account
has. You may get permission denied errors when attempting to launch a game or application from a non system drive.

You will need to modify the security permissions on your disk. Ensure that user/principal SYSTEM has full
permissions on the disk.
