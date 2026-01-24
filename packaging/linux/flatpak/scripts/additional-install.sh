#!/bin/sh

# User Service
mkdir -p ~/.config/systemd/user
cp "/app/share/sunshine/systemd/user/sunshine.service" "$HOME/.config/systemd/user/sunshine.service"
cp "/app/share/sunshine/systemd/user/sunshine-kms.service" "$HOME/.config/systemd/user/sunshine-kms.service"
echo "Sunshine User Services have been installed."
echo "Use [systemctl --user enable sunshine] or [systemctl --user enable sunshine-kms] once to autostart Sunshine on login."

# Load uhid (DS5 emulation)
UHID=$(cat /app/share/sunshine/modules-load.d/60-sunshine.conf)
echo "Enabling DS5 emulation."
flatpak-spawn --host pkexec sh -c "echo '$UHID' > /etc/modules-load.d/60-sunshine.conf"
flatpak-spawn --host pkexec modprobe uhid

# Udev rule
UDEV=$(cat /app/share/sunshine/udev/rules.d/60-sunshine.rules)
echo "Configuring mouse permission."
flatpak-spawn --host pkexec sh -c "echo '$UDEV' > /etc/udev/rules.d/60-sunshine.rules"
echo "Restart computer for mouse permission to take effect."
