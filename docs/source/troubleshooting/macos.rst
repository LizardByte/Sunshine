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
