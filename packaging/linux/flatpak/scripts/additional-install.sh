#!/bin/sh

set -e

# User Service
mkdir -p ~/.config/systemd/user
cp "/app/share/sunshine/systemd/user/app-dev.lizardbyte.app.Sunshine.service" "$HOME/.config/systemd/user/app-dev.lizardbyte.app.Sunshine.service"
echo "Sunshine User Service has been installed."
echo "Use [systemctl --user enable app-dev.lizardbyte.app.Sunshine] once to autostart Sunshine on login."

# Load uhid for descriptor-driven gamepad emulation
UHID=$(cat /app/share/sunshine/modules-load.d/60-sunshine.conf)
echo "Enabling gamepad emulation."
# The host shell expands $1; the configuration is passed as a literal argument.
# shellcheck disable=SC2016
flatpak-spawn --host pkexec sh -c 'printf "%s\n" "$1" > /etc/modules-load.d/60-sunshine.conf' sh "$UHID"
flatpak-spawn --host pkexec modprobe uhid

# Udev rule
UDEV=$(cat /app/share/sunshine/udev/rules.d/60-sunshine.rules)
echo "Configuring virtual input permissions."
# The host shell expands $1; the rules are passed as a literal argument.
# shellcheck disable=SC2016
flatpak-spawn --host pkexec sh -c 'printf "%s\n" "$1" > /etc/udev/rules.d/60-sunshine.rules' sh "$UDEV"
flatpak-spawn --host pkexec udevadm control --reload-rules
flatpak-spawn --host pkexec udevadm trigger --property-match=DEVNAME=/dev/uinput
flatpak-spawn --host pkexec udevadm trigger --property-match=DEVNAME=/dev/uhid
flatpak-spawn --host pkexec udevadm trigger --subsystem-match=hidraw
flatpak-spawn --host pkexec udevadm trigger --subsystem-match=input
echo "Virtual input permissions have been updated."
