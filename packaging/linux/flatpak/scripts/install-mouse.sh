#!/bin/sh

UDEV=$(cat /app/share/sunshine/udev/rules.d/85-sunshine.rules)
flatpak-spawn --host pkexec sh -c "usermod -a -G input $USER && echo '$UDEV' > /etc/udev/rules.d/85-sunshine.rules"
echo Restart computer for mouse permission to take effect.
