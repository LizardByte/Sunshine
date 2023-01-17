macOS
=====

Dynamic session lookup failed
-----------------------------
If you get this error:
    `Dynamic session lookup supported but failed: launchd did not provide a socket path, verify that
    org.freedesktop.dbus-session.plist is loaded!`

   Try this.
      .. code-block:: bash

         launchctl load -w /Library/LaunchAgents/org.freedesktop.dbus-session.plist

No gamepad detected
-------------------
#. Verify that you've installed `VirtualHID <https://github.com/kotleni/VirtualHID-macOS/releases/latest>`_.

Unable to create HID device
---------------------------
If you get this error:
    `Gamepad: Unable to create HID device. May be fine if created previously.`

   It's okay. It's just a warning.
