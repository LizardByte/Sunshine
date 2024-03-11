#!/bin/sh

# User Service
mkdir -p ~/.config/systemd/user
cp /app/share/sunshine/systemd/user/sunshine.service $HOME/.config/systemd/user/sunshine.service
echo Sunshine User Service has been installed.
echo Use [systemctl --user enable sunshine] once to autostart Sunshine on login.

# Udev rule
UDEV=$(cat /app/share/sunshine/udev/rules.d/60-sunshine.rules)
echo Configuring mouse permission.
flatpak-spawn --host pkexec sh -c "echo '$UDEV' > /etc/udev/rules.d/60-sunshine.rules"
echo Restart computer for mouse permission to take effect.
