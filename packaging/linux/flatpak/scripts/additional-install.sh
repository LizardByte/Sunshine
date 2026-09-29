#!/bin/sh

# User Service
mkdir -p ~/.config/systemd/user
cp "/app/share/sunshine/systemd/user/app-dev.lizardbyte.app.Sunshine.service" "$HOME/.config/systemd/user/app-dev.lizardbyte.app.Sunshine.service"
echo "Sunshine User Service has been installed."
echo "Use [systemctl --user enable app-dev.lizardbyte.app.Sunshine] once to autostart Sunshine on login."

# Load uhid for descriptor-driven gamepad emulation
UHID=$(cat /app/share/sunshine/modules-load.d/60-sunshine.conf)
echo "Enabling gamepad emulation."
flatpak-spawn --host pkexec sh -c "echo '$UHID' > /etc/modules-load.d/60-sunshine.conf"
flatpak-spawn --host pkexec modprobe uhid

# Udev rule
UDEV=$(cat /app/share/sunshine/udev/rules.d/60-sunshine.rules)
echo "Configuring virtual input permissions."
flatpak-spawn --host pkexec sh -c "echo '$UDEV' > /etc/udev/rules.d/60-sunshine.rules"
flatpak-spawn --host pkexec udevadm control --reload-rules
flatpak-spawn --host pkexec udevadm trigger --property-match=DEVNAME=/dev/uinput
flatpak-spawn --host pkexec udevadm trigger --property-match=DEVNAME=/dev/uhid
flatpak-spawn --host pkexec udevadm trigger --subsystem-match=hidraw
flatpak-spawn --host pkexec udevadm trigger --subsystem-match=input
echo "Virtual input permissions have been updated."
