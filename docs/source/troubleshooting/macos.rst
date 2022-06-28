:github_url: https://github.com/SunshineStream/Sunshine/tree/nightly/docs/source/troubleshooting/macos.rst

macOS
=====
If you get this error:

   ``Dynamic session lookup supported but failed: launchd did not provide a socket path, verify that
   org.freedesktop.dbus-session.plist is loaded!``

   Try this.

      .. code-block:: bash

         launchctl load -w /Library/LaunchAgents/org.freedesktop.dbus-session.plist

Uninstall:

   - pkg

      .. code-block:: bash

         sudo chmod +x /opt/local/etc/sunshine/assets/uninstall_pkg.sh
         sudo /opt/local/etc/sunshine/assets/uninstall_pkg.sh

   - Portfile

      .. code-block:: bash

         sudo port uninstall Sunshine
