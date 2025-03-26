#!/bin/sh

# User Service
systemctl --user stop sunshine
rm "${HOME}/.config/systemd/user/sunshine.service"
systemctl --user daemon-reload
echo Sunshine User Service has been removed.

# Udev rule
echo Removing input permissions.
flatpak-spawn --host pkexec sh -c "rm /etc/udev/rules.d/60-sunshine.rules"

# Reload udev rules
path_to_udevadm=$(flatpak-spawn --host which udevadm)
if [ -x "$path_to_udevadm" ] ; then
  echo "Reloading udev rules."
  flatpak-spawn --host "$path_to_udevadm" control --reload-rules
  flatpak-spawn --host "$path_to_udevadm" trigger --property-match=DEVNAME=/dev/uinput
  flatpak-spawn --host "$path_to_udevadm" trigger --property-match=DEVNAME=/dev/uhid
  echo "Udev rules reloadeded successfully."
else
  echo "error: udevadm not found or not executable."
fi
