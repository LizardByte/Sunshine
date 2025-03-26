#!/bin/sh

# User Service
mkdir -p ~/.config/systemd/user
cp /app/share/sunshine/systemd/user/sunshine.service "${HOME}/.config/systemd/user/sunshine.service"
echo Sunshine User Service has been installed.
echo Use [systemctl --user enable sunshine] once to autostart Sunshine on login.

# Udev rule
UDEV=$(cat /app/share/sunshine/udev/rules.d/60-sunshine.rules)
echo Configuring input permissions.
flatpak-spawn --host pkexec sh -c "echo '$UDEV' > /etc/udev/rules.d/60-sunshine.rules"

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
